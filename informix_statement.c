/*
  +----------------------------------------------------------------------+
  | (C) Copyright IBM Corporation 2006.                                  |
  +----------------------------------------------------------------------+
  |                                                                      |
  | Licensed under the Apache License, Version 2.0 (the "License"); you  |
  | may not use this file except in compliance with the License. You may |
  | obtain a copy of the License at                                      |
  | http://www.apache.org/licenses/LICENSE-2.0                           |
  |                                                                      |
  | Unless required by applicable law or agreed to in writing, software  |
  | distributed under the License is distributed on an "AS IS" BASIS,    |
  | WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or      |
  | implied. See the License for the specific language governing         |
  | permissions and limitations under the License.                       |
  +----------------------------------------------------------------------+
  | Authors: Rick McGuire, Dan Scott, Krishna Raman, Kellen Bombardier,  |
  | Ambrish Bhargava, Rahul Priyadarshi                                  |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "pdo/php_pdo.h"
#include "pdo/php_pdo_driver.h"
#include "php_pdo_informix.h"
#include "php_pdo_informix_int.h"

//# define SQL_LEN_DATA_AT_EXEC(length) length

struct lob_stream_data
{
	stmt_handle *stmt_res;
	pdo_stmt_t *stmt;
	int colno;
};


size_t lob_stream_read(php_stream *stream, char *buf, size_t count )
{
	SQLLEN readBytes = 0;
	struct lob_stream_data *data = stream->abstract;
	column_data *col_res = &data->stmt_res->columns[data->colno];
	stmt_handle *stmt_res = data->stmt_res;
	pdo_stmt_t *stmt = data->stmt;
	int ctype = 0;
	SQLRETURN rc = 0;

	switch (col_res->data_type) {
		default:
		case SQL_LONGVARCHAR:
			ctype = SQL_C_CHAR;
			break;
		case SQL_LONGVARBINARY:
		case SQL_VARBINARY:
		case SQL_BINARY:
		case SQL_INFX_UDT_BLOB:
		case SQL_INFX_UDT_CLOB:
			ctype = SQL_C_BINARY;
			break;
	}

	rc = SQLGetData(stmt_res->hstmt, data->colno + 1, ctype, buf, count, &readBytes);
	check_stmt_error(rc, "SQLGetData");

	if (readBytes == -1) {	/*For NULL CLOB/BLOB values */
		return (size_t) readBytes;
	}
	if (readBytes > count) {
		if (col_res->data_type == SQL_LONGVARCHAR) {  /*Dont return the NULL at end of CLOB buffer */
			readBytes = count - 1;
		} else {
			readBytes = count;
		}
	}
	return (size_t) readBytes;
}

size_t lob_stream_write(php_stream *stream, const char *buf, size_t count )
{
	return 0;
}

int lob_stream_flush(php_stream *stream )
{
	return 0;
}

int lob_stream_close(php_stream *stream, int close_handle )
{
	struct lob_stream_data *data = stream->abstract;
	efree(data);
	return 0;
}

php_stream_ops lob_stream_ops = {
	lob_stream_write,	/* Write */
	lob_stream_read,	/* Read */
	lob_stream_close,	/* Close */
	lob_stream_flush,	/* Flush */
	"informix PDO Lob stream",
	NULL,			/* Seek */
	NULL,			/* GetS */
	NULL,			/* Cast */
	NULL			/* Stat */
};

php_stream* create_lob_stream( pdo_stmt_t *stmt , stmt_handle *stmt_res , int colno  )
{
	struct lob_stream_data *data;
	column_data *col_res;
	php_stream *retval;

	data = emalloc(sizeof(struct lob_stream_data));
	data->stmt_res = stmt_res;
	data->stmt = stmt;
	data->colno = colno;
	col_res = &data->stmt_res->columns[data->colno];
	retval = (php_stream *) php_stream_alloc(&lob_stream_ops, data, NULL, "r");
	/* Find out if the column contains NULL data */
	if (lob_stream_read(retval, NULL, 0 ) == SQL_NULL_DATA) {
		php_stream_close(retval);
		return NULL;
	} else
		return retval;
}

/*
* Clear up our column descriptors.  This is done either from
* the statement constructors or whenever we traverse from one
* result set to the next.
*/
static void stmt_free_column_descriptors(pdo_stmt_t *stmt )
{
	stmt_handle *stmt_res = (stmt_handle *) stmt->driver_data;
	if (stmt_res->columns != NULL) {
		int i;
		/* see if any of the columns have attached storage too. */
		for (i = 0; i < stmt->column_count; i++) {
			/*
			 * Was this a string form?  We have an allocated string
			 * buffer that also needs releasing.
			 */
			if (stmt_res->columns[i].returned_type == PDO_PARAM_STR) {
				efree(stmt_res->columns[i].data.str_val);
			}
		}

		/* free the entire column list. */
		efree(stmt_res->columns);
		stmt_res->columns = NULL;
	}
}

/*
* Cleanup any driver-allocated control blocks attached to a statement
* instance.  This cleans up the driver_data control block, as
* well as any temporary allocations used during execution.
*/
void stmt_cleanup(pdo_stmt_t *stmt )
{
	stmt_handle *stmt_res = (stmt_handle *) stmt->driver_data;
	if (stmt_res != NULL) {
		if (stmt_res->converted_statement != NULL) {
#if PHP_8_1_OR_HIGHER
                        zend_string_release(stmt_res->converted_statement);
#else
			efree(stmt_res->converted_statement);
#endif
		}
		if (stmt_res->lob_buffer != NULL) {
			stmt_res->lob_buffer = NULL;
		}
		/* free any descriptors we're keeping active */
		stmt_free_column_descriptors(stmt );
		efree(stmt_res);
	}
	stmt->driver_data = NULL;
}

/* get the parameter description information for a positional bound parameter. */
static int stmt_get_parameter_info(pdo_stmt_t * stmt, struct pdo_bound_param_data *param 
		)
{
	param_node *param_res = (param_node *) param->driver_data;
	stmt_handle *stmt_res = NULL;
	int rc = 0;

	/* do we have the parameter information yet? */
	if (param_res == NULL) {
		/* allocate a new one and attach to the PDO param structure */
		param_res = (param_node *) emalloc(sizeof(param_node));
		check_stmt_allocation(param_res, "stmt_get_parameter",
				"Unable to allocate parameter driver data");

		/* get the statement specifics */
		stmt_res = (stmt_handle *) stmt->driver_data;

		/* checks the server version for correct SQL column meta call */
		if (stmt_res->server_ver >= 94) {
			/*
			* NB:  The PDO parameter numbers are origin zero, but the
			* SQLDescribeParam() ones start with 1.
			*/
			rc = SQLDescribeParam((SQLHSTMT) stmt_res->hstmt,
					(SQLUSMALLINT) param->paramno + 1,
					&param_res->data_type,
					&param_res->param_size, &param_res->scale, &param_res->nullable);
			/* Free the memory if SQLDescribeParam failed */
			if (rc == SQL_ERROR) {
				efree(param_res);
				param_res = NULL;
			}
			check_stmt_error(rc, "SQLDescribeParam");
		} else {
			param_res->data_type = SQL_C_CHAR;
		}

		/* only attach this if we succeed */
		param->driver_data = param_res;

		/*
		* but see if we need to alter this for binary forms or
		* can optimize numerics a little.
		*/
		switch (param_res->data_type) {
			/*
			* The binary forms need to be transferred as binary
			* data, not as char data.
			*/
			case SQL_BINARY:
			case SQL_INFX_UDT_BLOB:
			case SQL_INFX_UDT_CLOB:
			case SQL_VARBINARY:
			case SQL_LONGVARBINARY:
				param_res->ctype = SQL_C_BINARY;
				break;

			/*
			* Numeric forms we can map directly to a long
			* int value
			*/
			case SQL_SMALLINT:
			case SQL_INTEGER:
				param_res->ctype = SQL_C_LONG;
				break;

			/* everything else will transfer as binary */
			default:
				/* by default, we transfer as character data */
				param_res->ctype = SQL_C_CHAR;
				break;
		}
	}
	return TRUE;
}

/*
* Bind a statement parameter to the PHP value supplying or receiving the
* parameter data.
*/
int stmt_bind_parameter(pdo_stmt_t *stmt, struct pdo_bound_param_data *curr )
{
	int rc, is_num = 0;
	stmt_handle *stmt_res = (stmt_handle *) stmt->driver_data;
	param_node *param_res = NULL;
	SQLSMALLINT inputOutputType;

	/* make sure we have current description information. */
	if (stmt_get_parameter_info(stmt, curr ) == FALSE) {
		return FALSE;
	}

	param_res = (param_node *) curr->driver_data;

	/*
	* Now figure out the parameter type so we can tell the database code
	* how to handle this.
	* this is rare, really only used for stored procedures.
	*/
	if (curr->param_type & PDO_PARAM_INPUT_OUTPUT > 0) {
		inputOutputType = SQL_PARAM_INPUT_OUTPUT;
	}
	/*
	* If this is a non-positive length, we can't assign a value,
	* so this is by definition an INPUT param.
	*/
	else if (curr->max_value_len <= 0) {
		inputOutputType = SQL_PARAM_INPUT;
	} else {
	/* everything else is output. */
		inputOutputType = SQL_PARAM_OUTPUT;
	}

	/*
	* Now do the actual binding, which is controlled by the
	* PDO supplied type.
	*/
	switch (PDO_PARAM_TYPE(curr->param_type)) {
		/* not implemented yet */
		case PDO_PARAM_STMT:
			RAISE_INFORMIX_STMT_ERROR("IM001", "param_hook",
				"Driver does not support statement parameters");
			return FALSE;

		/* this is a long value for PHP */
		case PDO_PARAM_INT:
		/*
		* If the parameter type is a numeric type, we'll bind this
		* directly,
		*/
			if (param_res->ctype == SQL_C_LONG) {
#if PHP_MAJOR_VERSION >= 7
                                zval *parameter;
                                if (Z_ISREF(curr->parameter)) {
                                        parameter = Z_REFVAL(curr->parameter);
                                } else {
                                        parameter = &curr->parameter;
                                }
                                if ( parameter == NULL || Z_TYPE_P(parameter) == IS_NULL ) {
#else

				if (Z_TYPE_P(curr->parameter) == IS_NULL) {
#endif
					/* null value was found */
					param_res->transfer_length = SQL_NULL_DATA;
					rc = SQLBindParameter(stmt_res->hstmt,
							curr->paramno + 1,
							inputOutputType,
							param_res->ctype,
							param_res->data_type,
							param_res->param_size,
							param_res->scale, NULL,
							curr->max_value_len <=
							0 ? 0 : curr->max_value_len,
							&param_res->transfer_length);
					check_stmt_error(rc, "SQLBindParameter");
					return TRUE;
				} else {
#if PHP_MAJOR_VERSION >= 7
					convert_to_string(parameter);
#else
					convert_to_string(curr->parameter);
#endif
#if PHP_MAJOR_VERSION >= 7
					if (!strcmp(Z_STRVAL_P(parameter), "")) {
#else
					if (!strcmp(curr->parameter->value.str.val, "")) {
#endif
						/* empty string was found */
						param_res->transfer_length = SQL_NULL_DATA;
						rc = SQLBindParameter(stmt_res->hstmt,
								curr->paramno + 1,
								inputOutputType,
								param_res->ctype,
								param_res->data_type,
								param_res->param_size,
								param_res->scale, NULL,
								curr->max_value_len <=
								0 ? 0 : curr->max_value_len,
								&param_res->transfer_length);
						check_stmt_error(rc, "SQLBindParameter");
						return TRUE;
					} else {
						/* force this to be a real boolean value */
#if PHP_MAJOR_VERSION >= 7
						convert_to_long(parameter);
#else
						convert_to_long(curr->parameter);
#endif
						rc = SQLBindParameter(stmt_res->hstmt,
								curr->paramno + 1,
								inputOutputType, SQL_C_LONG,
								param_res->data_type,
								param_res->param_size,
								param_res->scale,
#if PHP_MAJOR_VERSION >= 7
							        &Z_LVAL_P(parameter),	
#else
								&((curr->parameter)->value.lval),
#endif
								0, NULL);
						check_stmt_error(rc, "SQLBindParameter");
						return TRUE;
					}
				}
			}

		/*
		* NOTE:  We fall through from above if there is a
		* type mismatch.
		*/

		/* a string value (very common) */
		case PDO_PARAM_BOOL:
		case PDO_PARAM_STR:
			/*
			* If we're capable of handling an integer value, but
			* PDO  is telling us string, then change this now.
			*/
			if (param_res->ctype == SQL_C_LONG) {
				/* change this to a character type */
				param_res->ctype = SQL_C_CHAR;
				is_num = 1;
			}
#if PHP_MAJOR_VERSION >= 7
                                zval *parameter;
                                if (Z_ISREF(curr->parameter)) {
                                        parameter = Z_REFVAL(curr->parameter);
                                } else {
                                        parameter = &curr->parameter;
                                }
			if (Z_TYPE_P(parameter) == IS_NULL
					|| (is_num && Z_STRVAL_P(parameter) != NULL
					&& (Z_STRVAL_P(parameter) == '\0'))) {
#else
			if (Z_TYPE_P(curr->parameter) == IS_NULL
					|| (is_num && Z_STRVAL_P(curr->parameter) != NULL
					&& (Z_STRVAL_P(curr->parameter) == '\0'))) {
#endif
				if (PDO_PARAM_TYPE(curr->param_type) == PDO_PARAM_BOOL ) {
					param_res->ctype = SQL_C_LONG;
				} else {
					param_res->ctype = SQL_C_CHAR;
				}
				param_res->param_size = 0;
				param_res->scale = 0;
				curr->max_value_len = 0;
				param_res->transfer_length = SQL_NULL_DATA;
				rc = SQLBindParameter(stmt_res->hstmt, curr->paramno + 1,
						inputOutputType, param_res->ctype,
						param_res->data_type,
						param_res->param_size,
						param_res->scale,
#if PHP_MAJOR_VERSION >= 7
					        Z_STRVAL_P(parameter), 	
#else
						&((curr->parameter)->value.lval),
#endif
						curr->max_value_len,
						&param_res->transfer_length);
				check_stmt_error(rc, "SQLBindParameter");
			} else {
				/* force this to be a real string value */
#if PHP_MAJOR_VERSION >= 7
				convert_to_string(parameter);
#else
				convert_to_string(curr->parameter);
#endif
				/*
				* The transfer length to zero now...this
				* gets updated at EXEC_PRE time.
				*/
				param_res->transfer_length = 0;

#if PHP_MAJOR_VERSION >= 7
				param_res->param_size = Z_STRLEN_P(parameter);
#else
				param_res->param_size = Z_STRLEN_P(curr->parameter);
#endif

				/*
				* Now we need to make sure the string buffer
				* is large enough to receive a new value if
				* this is an output or in/out parameter
				*/
#if PHP_MAJOR_VERSION >= 7
				if (inputOutputType != SQL_PARAM_INPUT &&
						curr->max_value_len > Z_STRLEN_P(parameter)) {
					/* reallocate this to the new size */
					Z_PTR_P(parameter) = erealloc(Z_STRVAL_P(parameter),
							curr->max_value_len + 1);
					check_stmt_allocation(Z_STRVAL_P(parameter),
							"stmt_bind_parameter",
							"Unable to allocate bound parameter");
				}
#else
				if (inputOutputType != SQL_PARAM_INPUT &&
						curr->max_value_len > Z_STRLEN_P(curr->parameter)) {
					/* reallocate this to the new size */
					Z_STRVAL_P(curr->parameter) = erealloc(Z_STRVAL_P(curr->parameter),
							curr->max_value_len + 1);
					check_stmt_allocation(Z_STRVAL_P(curr->parameter),
							"stmt_bind_parameter",
							"Unable to allocate bound parameter");
				}

#endif
				rc = SQLBindParameter(stmt_res->hstmt, curr->paramno + 1,
						inputOutputType, param_res->ctype,
						param_res->data_type,
						param_res->param_size,
						param_res->scale,
#if PHP_MAJOR_VERSION >= 7
						Z_STRVAL_P(parameter),
#else
						Z_STRVAL_P(curr->parameter),
#endif
						curr->max_value_len <=
						0 ? 0 : curr->max_value_len,
						&param_res->transfer_length);
				check_stmt_error(rc, "SQLBindParameter");
			}

			return TRUE;

		/*
		* This is either a string, or, if the length is zero,
		* then this is a pointer to a PHP stream.
		*/
		case PDO_PARAM_LOB:
			if (inputOutputType != SQL_PARAM_INPUT) {
				inputOutputType = SQL_PARAM_INPUT;
			}

			/* have we bound a LOB to a long type for some reason? */
			if (param_res->ctype == SQL_C_LONG) {
				/* transfer this as character data. */
				param_res->ctype = SQL_C_CHAR;
			}
			if (param_res->data_type == SQL_INFX_UDT_BLOB ||
				param_res->data_type == SQL_INFX_UDT_CLOB) {
				/* transfer this as binary data. */
				param_res->ctype = SQL_C_BINARY;
			}

			/* indicate we're going to transfer the data at exec time. */
			param_res->transfer_length = SQL_DATA_AT_EXEC;

			/*
			* We can't bind LOBs at this point...we process all
			* of this at execute time. However, we set the value
			* data to the PDO binding control block and set the
			* SQL_DATA_AT_EXEC value to cause it to prompt us for
			* the data at execute time. The pointer is recoverable
			* at that time by using SQLParamData(), and we can
			* then process the request.
			*/
			rc = SQLBindParameter(stmt_res->hstmt, curr->paramno + 1,
					inputOutputType, param_res->ctype,
					param_res->data_type,
					param_res->param_size, param_res->scale,
					(SQLPOINTER) curr,
					4096,
					&param_res->transfer_length);
			check_stmt_error(rc, "SQLBindParameter");
			return TRUE;

		/* this is an unknown type */
		default:
			RAISE_INFORMIX_STMT_ERROR( "IM001", "SQLBindParameter", "Unknown parameter type" );
			return FALSE;
	}

	return TRUE;
}

/* handle the pre-execution phase for bound parameters. */
static int stmt_parameter_pre_execute(pdo_stmt_t *stmt, struct pdo_bound_param_data *curr )
{
	stmt_handle *stmt_res = (stmt_handle *) stmt->driver_data;
	param_node *param_res = (param_node *) curr->driver_data;

	/*
	* Now we need to prepare the parameter binding information
	* for execution.  If this is a LOB, then we need to ensure
	* the LOB data is going to be available and make sure
	* the binding is tagged to provide the data at exec time.
	*/
#if PHP_MAJOR_VERSION >= 7
        zval *parameter;
        if (Z_ISREF(curr->parameter)) {
          parameter = Z_REFVAL(curr->parameter);
        } else {
          parameter = &curr->parameter;
        }
#endif
	if (PDO_PARAM_TYPE(curr->param_type) == PDO_PARAM_LOB) {
		/*
		* If the LOB data is a stream, we need to make sure it is
		* really there.
		*/
#if PHP_MAJOR_VERSION >= 7
                if (Z_TYPE_P(parameter) == IS_RESOURCE)
#else 
		if (Z_TYPE_P(curr->parameter) == IS_RESOURCE)
#endif
                {
			php_stream *stm;
			php_stream_statbuf sb;

			/* make sure we have a stream to work with */
#if PHP_MAJOR_VERSION >= 7
                        php_stream_from_zval_no_verify(stm, parameter);
#else 
			php_stream_from_zval_no_verify(stm, &curr->parameter);
#endif

			if (stm == NULL) {
				RAISE_INFORMIX_STMT_ERROR( "HY000" , "SQLBindParameter" ,
					"PDO_PARAM_LOB file stream is invalid");
			}

			/*
			* Now see if we can retrieve length information from
			* the stream
			*/
			if (php_stream_stat(stm, &sb) == 0) {
				/*
				* Yes, we're able to give the statement some
				* hints about the size.
				*/
				param_res->transfer_length = SQL_LEN_DATA_AT_EXEC(sb.sb.st_size);
			} else {
				/*
				* Still unknown...we'll have to do everything
				* at execute size.
				*/
				param_res->transfer_length = SQL_LEN_DATA_AT_EXEC(0);
			}
		} else {
			/*
			* Convert this to a string value now.  We bound the
			* data pointer to our parameter descriptor, so we
			* can't just supply this directly yet, but we can
			* at least give the size hint information.
			*/
#if PHP_MAJOR_VERSION >= 7
			convert_to_string(parameter);
			param_res->transfer_length = SQL_LEN_DATA_AT_EXEC(Z_STRLEN_P(parameter));
#else
			convert_to_string(curr->parameter);
			param_res->transfer_length = SQL_LEN_DATA_AT_EXEC(Z_STRLEN_P(curr->parameter));
#endif
		}

	} else {
#if PHP_MAJOR_VERSION >= 7
		if (Z_TYPE_P(parameter) != IS_NULL && param_res != NULL)
#else
		if (Z_TYPE_P(curr->parameter) != IS_NULL && param_res != NULL)
#endif
                {
			/*
			* if we're processing this as string or binary data,
			* then directly update the length to the real value.
			*/
			if (param_res->ctype == SQL_C_LONG) {
				/* make sure this is a long value */
#if PHP_MAJOR_VERSION >= 7
				convert_to_long(parameter);
#else
				convert_to_long(curr->parameter);
#endif
			} else {
				/*
				* Make sure this is a string value...it might
				* have been changed between the bind and the
				* execute
				*/
#if PHP_MAJOR_VERSION >= 7
				convert_to_string(parameter);
				param_res->transfer_length = Z_STRLEN_P(parameter);
#else
				convert_to_string(curr->parameter);
				param_res->transfer_length = Z_STRLEN_P(curr->parameter);
#endif
			}
		}
	}
	return TRUE;
}

/* post-execution bound parameter handling. */
static int stmt_parameter_post_execute(pdo_stmt_t *stmt, struct pdo_bound_param_data *curr )
{
	param_node *param_res = (param_node *) curr->driver_data;

	/*
	* If the type of the parameter is a string, we need to update the
	* string length and make sure that these are null terminated.
	* Values returned from the DB are just copied directly into the bound
	* locations, so we need to update the PHP control blocks so that the
	* data is processed correctly.
	*/
#if PHP_MAJOR_VERSION >= 7

        zval *parameter;
        if (Z_ISREF(curr->parameter)) {
              parameter = Z_REFVAL(curr->parameter);
        } else {
              parameter = &curr->parameter;
        }
	if (Z_TYPE_P(parameter) == IS_STRING) {
		if (param_res->transfer_length < 0 || param_res->transfer_length == SQL_NULL_DATA) {
			Z_STRLEN_P(parameter) = 0;
			Z_STRVAL_P(parameter)[0] = '\0';
		} else if (param_res->transfer_length == 0) {
			ZVAL_EMPTY_STRING(parameter);
		} else {
			Z_STRLEN_P(parameter) = param_res->transfer_length;
			Z_STRVAL_P(parameter)[param_res->transfer_length] = '\0';
		}
	}
#else
	if (Z_TYPE_P(curr->parameter) == IS_STRING) {
		if (param_res->transfer_length < 0 || param_res->transfer_length == SQL_NULL_DATA) {
			Z_STRLEN_P(curr->parameter) = 0;
			Z_STRVAL_P(curr->parameter)[0] = '\0';
		} else if (param_res->transfer_length == 0) {
			ZVAL_EMPTY_STRING(curr->parameter);
		} else {
			Z_STRLEN_P(curr->parameter) = param_res->transfer_length;
			Z_STRVAL_P(curr->parameter)[param_res->transfer_length] = '\0';
		}
	}
#endif
	return TRUE;
}

/* bind a column to an internally allocated buffer location. */
static int stmt_bind_column(pdo_stmt_t *stmt, int colno )
{
	stmt_handle *stmt_res;
	column_data *col_res;
	struct pdo_column_data *col;
	int rc;
	SQLLEN in_length = 1;
	stmt_res = (stmt_handle *) stmt->driver_data;
	col_res = &stmt_res->columns[colno];
	col = &stmt->columns[colno];

	switch (col_res->data_type) {
		case SQL_LONGVARCHAR:
		case SQL_LONGVARBINARY:
		case SQL_VARBINARY:
		case SQL_BINARY:
		case SQL_INFX_UDT_BLOB:
		case SQL_INFX_UDT_CLOB:
			{
				/* we're going to need to do getdata calls to retrieve these */
				col_res->out_length = 0;
				/* and this is returned as a stream */
				col_res->returned_type = PDO_PARAM_LOB;
#if !PHP_8_1_OR_HIGHER 
				col->param_type = PDO_PARAM_LOB;
#endif
			}
			break;
		/*
		* An extra byte is required to hold positive or negative value if the
		* data type is INTERVAL. That is why we are increasing in_length by 1.
		*/
		case SQL_INTERVAL_YEAR:
		case SQL_INTERVAL_MONTH:
		case SQL_INTERVAL_DAY:
		case SQL_INTERVAL_HOUR:
		case SQL_INTERVAL_MINUTE:
		case SQL_INTERVAL_SECOND:
		case SQL_INTERVAL_YEAR_TO_MONTH:
		case SQL_INTERVAL_DAY_TO_HOUR:
		case SQL_INTERVAL_DAY_TO_MINUTE:
		case SQL_INTERVAL_DAY_TO_SECOND:
		case SQL_INTERVAL_HOUR_TO_MINUTE:
		case SQL_INTERVAL_HOUR_TO_SECOND:
		case SQL_INTERVAL_MINUTE_TO_SECOND:
			in_length = in_length + 1;
		/*
		* A form we need to force into a string value...
		* this includes any unknown types
		*/
		case SQL_CHAR:
		case SQL_VARCHAR:
		case SQL_TYPE_TIME:
		case SQL_TYPE_TIMESTAMP:
		case SQL_BIGINT:
		case SQL_REAL:
		case SQL_FLOAT:
		case SQL_DOUBLE:
		case SQL_DECIMAL:
		case SQL_NUMERIC:
		default:
			in_length = ( col_res->data_size + in_length ) * 4;
			col_res->data.str_val = (char *) emalloc(in_length+1);
			check_stmt_allocation(col_res->data.str_val,
					"stmt_bind_column",
					"Unable to allocate column buffer");
			col_res->data.str_val[in_length] = '\0';
			rc = SQLBindCol((SQLHSTMT) stmt_res->hstmt,
					(SQLUSMALLINT) (colno + 1), SQL_C_CHAR,
					col_res->data.str_val, in_length,
					(SQLLEN *) (&col_res->out_length));
			col_res->returned_type = PDO_PARAM_STR;
#if !PHP_8_1_OR_HIGHER
			col->param_type = PDO_PARAM_STR;
#endif
	}
	return TRUE;
}

/* allocate a set of internal column descriptors for a statement. */
static int stmt_allocate_column_descriptors(pdo_stmt_t *stmt )
{
	stmt_handle *stmt_res = (stmt_handle *) stmt->driver_data;
	SQLSMALLINT nResultCols = 0;

	/* not sure */
	int rc = SQLNumResultCols((SQLHSTMT) stmt_res->hstmt, &nResultCols);
	check_stmt_error(rc, "SQLNumResultCols");

	/*
	* Make sure we set the count in the PDO stmt structure so the driver
	* knows how many columns we're dealing with.
	*/
	stmt->column_count = nResultCols;

	/*
	* Allocate the column descriptors now.  We'll bind each column
	* individually before the first fetch.  The binding process will
	* allocate any additional buffers we might need for the data.
	*/
	stmt_res->columns = (column_data *) ecalloc(sizeof(column_data), stmt->column_count);
	check_stmt_allocation(stmt_res->columns, "stmt_allocate_column_descriptors",
			"Unable to allocate column descriptor tables");
	memset(stmt_res->columns, '\0', sizeof(column_data) * stmt->column_count);
	return TRUE;
}

/*
* This is also used for error cleanup for errors that occur while
* the stmt is still half constructed.
*/
int informix_stmt_dtor( pdo_stmt_t *stmt )
{
	stmt_handle *stmt_res = (stmt_handle *) stmt->driver_data;

	if (stmt_res != NULL) {
		if (stmt_res->hstmt != SQL_NULL_HANDLE) {
			/* if we've done some work, we need to clean up. */
			if (stmt->executed) {
				/* cancel anything we have pending at this point */
				SQLCancel(stmt_res->hstmt);
			}
			SQLFreeHandle(SQL_HANDLE_STMT, stmt_res->hstmt);
			stmt_res->hstmt = SQL_NULL_HANDLE;
		}
		/* release any control blocks we have attached to this statement */
		stmt_cleanup(stmt );
	}
	return TRUE;
}

/*
* Execute a PDOStatement.  Used for both the PDOStatement::execute() method
* as well as the PDO:query() method.
*/
static int informix_stmt_executer( pdo_stmt_t * stmt )
{
	stmt_handle *stmt_res = (stmt_handle *) stmt->driver_data;
	int rc = 0;
	SQLLEN rowCount;

	/*
	* If this statement has already been executed, then we need to
	* cancel the previous execution before doing this again.
	*/
	if (stmt->executed) {
		rc = SQLFreeStmt(stmt_res->hstmt, SQL_CLOSE);
		check_stmt_error(rc, "SQLFreeStmt");
	}

	/*
	* We're executing now...this tells error handling to Cancel
	* if there's an error.
	*/
	stmt_res->executing = 1;

	/* clear the current error information to get ready for new execute */
	clear_stmt_error(stmt);

	stmt_res->lob_buffer = NULL;
	/*
	* Execute the statement.  All parameters should be bound at
	* this point, but we might need to pump data in for some of
	* the parameters.
	*/
	rc = SQLExecute((SQLHSTMT) stmt_res->hstmt);
	check_stmt_error(rc, "SQLExecute");
	/*
	* Now check if we have indirectly bound parameters. If we do,
	* then we need to push the data for those parameters into the
	* processing pipe.
	*/

	if (rc == SQL_NEED_DATA) {
		struct pdo_bound_param_data *param;

		/*
		* Get the associated parameter data.  The bind process should have
		* stored a pointer to the parameter control block, so we identify
		* which one needs data from that.
		*/
		while ((SQLParamData(stmt_res->hstmt, (SQLPOINTER) & param)) == SQL_NEED_DATA) {
	
			/*
			* OK, we have a LOB.  This is either in string form, in
			* which case we can supply it directly, or is a PHP stream.
			* If it is a stream, then the type is IS_RESOURCE, and we
			* need to pump the data in a buffer at a time.
			*/
#if PHP_MAJOR_VERSION >= 7
                        zval *parameter;
                        if (Z_ISREF(param->parameter)) {
                            parameter = Z_REFVAL(param->parameter);
                        } else {
                            parameter = &param->parameter;
                        }

			if (Z_TYPE_P(parameter) != IS_RESOURCE) {
				convert_to_string(parameter);
				rc = SQLPutData(stmt_res->hstmt, Z_STRVAL_P(parameter),
						Z_STRLEN_P(parameter));
				check_stmt_error(rc, "SQLPutData");
				continue;
#else
			if (Z_TYPE_P(param->parameter) != IS_RESOURCE) {
				convert_to_string(param->parameter);
				rc = SQLPutData(stmt_res->hstmt, Z_STRVAL_P(param->parameter),
						Z_STRLEN_P(param->parameter));
				check_stmt_error(rc, "SQLPutData");
				continue;
#endif
			} else {
				/*
				* The LOB is a stream.  This better still be good, else we
				* can't supply the data.
				*/
				php_stream *stm = NULL;
				int len;
#if PHP_MAJOR_VERSION >= 7
				php_stream_from_zval_no_verify(stm, (parameter));
#else
				php_stream_from_zval_no_verify(stm, &(param->parameter));
#endif
				if (!stm) {
					RAISE_INFORMIX_STMT_ERROR("HY000", "execute",
						"Input parameter LOB is no longer a valid stream");
					return FALSE;
				}
				/* allocate a buffer if we haven't prior to this */
				if (stmt_res->lob_buffer == NULL) {
					stmt_res->lob_buffer = emalloc(LOB_BUFFER_SIZE);
					check_stmt_allocation(stmt_res->lob_buffer,
						"stmt_execute", "Unable to allocate parameter data buffer");
				}
				/* read a buffer at a time and push into the execution pipe. */
				for (;;) {
					len = php_stream_read(stm, stmt_res->lob_buffer, LOB_BUFFER_SIZE);
					if (len == 0) {
						break;
					}
					/* add the buffer */
					rc = SQLPutData(stmt_res->hstmt, stmt_res->lob_buffer, len);
					check_stmt_error(rc, "SQLPutData");
				}
			}
		}
		/* Free any LOB buffer we might have */
		if (stmt_res->lob_buffer != NULL) {
			efree(stmt_res->lob_buffer);
		}
	}
	else
	{
		/*
		*  Now set the rowcount field in the statement.  This will be the
		* number of rows affected by the SQL statement, not the number of
		* rows in the result set.
		*/
		rc = SQLRowCount(stmt_res->hstmt, &rowCount);
		check_stmt_error(rc, "SQLRowCount");
		/* store the affected rows information. */
		stmt->row_count = rowCount;
	
		/* Is this the first time we've executed this statement? */
		if (!stmt->executed) {
			if (stmt_allocate_column_descriptors(stmt ) == FALSE) {
				return FALSE;
			}
		}
	}

	/* Set the last serial id inserted */
	rc = record_last_insert_id(stmt->dbh, stmt_res->hstmt );
	if( rc == SQL_ERROR ) {
		return FALSE;
	}

	/* we can turn off the cleanup flag now */
	stmt_res->executing = 0;

	return TRUE;
}

/* fetch the next row of the result set. */
static int informix_stmt_fetcher(
	pdo_stmt_t *stmt,
	enum pdo_fetch_orientation ori,
	long offset
	)
{
	stmt_handle *stmt_res = (stmt_handle *) stmt->driver_data;
	/* by default, we're just fetching the next one */
	SQLSMALLINT direction = SQL_FETCH_NEXT;
	int rc = 0;

	/* convert the PDO orientation information to the SQL one */
	switch (ori) {
		case PDO_FETCH_ORI_NEXT:
			direction = SQL_FETCH_NEXT;
			break;
		case PDO_FETCH_ORI_PRIOR:
			direction = SQL_FETCH_PRIOR;
			break;
		case PDO_FETCH_ORI_FIRST:
			direction = SQL_FETCH_FIRST;
			break;
		case PDO_FETCH_ORI_LAST:
			direction = SQL_FETCH_LAST;
			break;
		case PDO_FETCH_ORI_ABS:
			direction = SQL_FETCH_ABSOLUTE;
			break;
		case PDO_FETCH_ORI_REL:
			direction = SQL_FETCH_RELATIVE;
			break;
	}

	/* go fetch it. */
	rc = SQLFetchScroll((SQLHSTMT)stmt_res->hstmt, direction, (SQLINTEGER) offset);
	check_stmt_error(rc, "SQLFetchScroll");

	/*
	* The fetcher() routine has an overloaded return value.
	* A false return can indicate either an error or the end
	* of the row data.
	*/
	if (rc == SQL_NO_DATA) {
		/*
		* We are scrolling forward, then close the cursor
		* to release resources tied up by this statement.
		*/
		if (stmt_res->cursor_type == PDO_CURSOR_FWDONLY) {
			SQLCloseCursor(stmt_res->hstmt);
		}
		return FALSE;
	} else if (rc == SQL_ERROR) {
		return FALSE;
	}

	return TRUE;
}

/* process the various bound parameter events. */
static int informix_stmt_param_hook(
	pdo_stmt_t *stmt,
	struct pdo_bound_param_data *param,
	enum pdo_param_event event_type
	)
{
	/*
	* We get called for both parameters and bound columns.
	* We only need to process the parameters
	*/
	if (param->is_param) {
		switch (event_type) {
			case PDO_PARAM_EVT_ALLOC:
				break;

			case PDO_PARAM_EVT_FREE:
			/*
			* During the alloc event, we attached some driver
			* specific data.  We need to free this now.
			*/
				if (param->driver_data != NULL) {
					efree(param->driver_data);
					param->driver_data = NULL;
				}
				break;

			case PDO_PARAM_EVT_EXEC_PRE:
			/* we're allocating a bound parameter, go do the binding */
				stmt_bind_parameter(stmt, param );
				return stmt_parameter_pre_execute(stmt, param );
			case PDO_PARAM_EVT_EXEC_POST:
				return stmt_parameter_post_execute(stmt, param );

			/* parameters aren't processed at the fetch phase. */
			case PDO_PARAM_EVT_FETCH_PRE:
			case PDO_PARAM_EVT_FETCH_POST:
                        /* XXX: anything we can actually normalize? */
                        case PDO_PARAM_EVT_NORMALIZE:
				break;
		}
	} else {
		switch (event_type) {
			case PDO_PARAM_EVT_ALLOC:
				break;
			case PDO_PARAM_EVT_FREE:
				break;
			case PDO_PARAM_EVT_EXEC_PRE:
				break;
			case PDO_PARAM_EVT_EXEC_POST:
				break;
			case PDO_PARAM_EVT_FETCH_PRE:
				if (param->param_type == PDO_PARAM_LOB) {
					(&((stmt_handle *) stmt->driver_data)->
						columns[param->paramno])->returned_type = PDO_PARAM_LOB;
				}
				break;
			case PDO_PARAM_EVT_FETCH_POST:
				break;
                        case PDO_PARAM_EVT_NORMALIZE:
                                break;
		}
	}
	return TRUE;
}

/* describe a column for the PDO driver. */
static int informix_stmt_describer(
	pdo_stmt_t *stmt,
	int colno
	)
{
	stmt_handle *stmt_res = (stmt_handle *)stmt->driver_data;
	/* access the information for this column */
	column_data *col_res = &stmt_res->columns[colno];
	struct pdo_column_data *col = NULL;
	char tmp_name[BUFSIZ];

	/* get the column descriptor information */
	int rc = SQLDescribeCol((SQLHSTMT)stmt_res->hstmt, (SQLSMALLINT)(colno + 1 ),
			tmp_name, BUFSIZ, &col_res->namelen, &col_res->data_type, &col_res->data_size,
			&col_res->scale, &col_res->nullable);
	check_stmt_error(rc, "SQLDescribeCol");

	rc = SQLColAttribute(stmt_res->hstmt, colno+1, SQL_DESC_DISPLAY_SIZE,
			NULL, 0, NULL, &col_res->data_size);
	check_stmt_error(rc, "SQLColAttribute");
	/*
	* Make sure we get a name properly.  If the name is too long for our
	* buffer (which in theory should never happen), allocate a longer one
	* and ask for the information again.
	*/
	if (col_res->namelen <= 0) {
#if PHP_MAJOR_VERSION >= 7
                col_res->name =  zend_string_init("", 0, 0);
#else
		col_res->name = estrdup("");
#endif
		check_stmt_allocation(col_res->name, "informix_stmt_describer",
				"Unable to allocate column name");
	} else if (col_res->namelen >= BUFSIZ ) {
		/* column name is longer than BUFSIZ */
		col_res->name = emalloc(col_res->namelen + 1);
		check_stmt_allocation(col_res->name, "informix_stmt_describer", "Unable to allocate column name");
		rc = SQLDescribeCol((SQLHSTMT)stmt_res->hstmt, (SQLSMALLINT)(colno + 1 ), tmp_name,
				BUFSIZ, &col_res->namelen, &col_res->data_type, &col_res->data_size, &col_res->scale,
				&col_res->nullable);
		check_stmt_error(rc, "SQLDescribeCol");
	} else {
#if PHP_MAJOR_VERSION >= 7
                col_res->name = zend_string_init(tmp_name, col_res->namelen, 0);
#else
		col_res->name = estrdup(tmp_name);
#endif
		check_stmt_allocation(col_res->name, "informix_stmt_describer", "Unable to allocate column name");
	}
	col = &stmt->columns[colno];

	/*
	* Copy the information back into the PDO control block.  Note that
	* PDO will release the name information, so we don't have to.
	*/
	col->name = col_res->name;
#if PHP_MAJOR_VERSION < 7
	col->namelen = col_res->namelen;
#endif
	col->maxlen = col_res->data_size;
	col->precision = col_res->scale;

	/* bind the columns */
	stmt_bind_column(stmt, colno );
	return TRUE;
}

/*
* Fetch the data for a specific column.  This should be sitting in our
* allocated buffer already, and easy to return.
*/
static int informix_stmt_get_col(
	pdo_stmt_t *stmt,
	int colno,
#if PHP_8_1_OR_HIGHER
	zval *result,
	enum pdo_param_type *type)
#else
	char **ptr,
	unsigned long *len,
	int *caller_frees)
#endif
{
	stmt_handle *stmt_res = (stmt_handle *) stmt->driver_data;
	/* access our look aside data */
	column_data *col_res = &stmt_res->columns[colno];

	if (col_res->returned_type == PDO_PARAM_LOB) {
		php_stream *stream = create_lob_stream(stmt, stmt_res, colno );	/* already opened */
#if PHP_8_1_OR_HIGHER
		php_stream_to_zval(stream, result);
#else
		if (stream != NULL) {
			*ptr = (char *) stream;
		} else {
			*ptr = NULL;
		}
		*len = 0;
#endif
	}
	/* see if this is a null value */
	else if (col_res->out_length == SQL_NULL_DATA) {
		/* return this as a real null */
#if PHP_8_1_OR_HIGHER
		ZVAL_NULL(result);
#else
		*ptr = NULL;
		*len = 0;
#endif
	}
	/* see if length is SQL_NTS ("count the length yourself"-value) */
	else if (col_res->out_length == SQL_NTS) {
		if (col_res->data.str_val && col_res->data.str_val[0] != '\0') {
			/* it's not an empty string */
#if PHP_8_1_OR_HIGHER
			ZVAL_STRING(result, col_res->data.str_val);
#else
			*ptr = col_res->data.str_val;
			*len = strlen(col_res->data.str_val);
#endif
		} else if (col_res->data.str_val && col_res->data.str_val[0] == '\0') {
			/* it's an empty string */
#if PHP_8_1_OR_HIGHER
			ZVAL_STRINGL(result, col_res->data.str_val, 0);
#else
			*ptr = col_res->data.str_val;
			*len = 0;
#endif
		} else {
			/* it's NULL */
#if PHP_8_1_OR_HIGHER
			ZVAL_NULL(result);
#else
			*ptr = NULL;
			*len = 0;
#endif
		}
	}
	/* string type...very common */
	else if (col_res->returned_type == PDO_PARAM_STR) {
		/* set the info */
        switch(col_res->data_type) {
            case SQL_INTEGER:
            case SQL_SMALLINT:
            case SQL_INFX_BIGINT:
                if (col_res->out_length > 20) {
#if PHP_8_1_OR_HIGHER
			ZVAL_NULL(result);
#else
                    *ptr = NULL;
                    *len = 0;
#endif
                    break;
                }
            default:
#if PHP_8_1_OR_HIGHER
		ZVAL_STRINGL(result, col_res->data.str_val, col_res->out_length);
#else
                *ptr = col_res->data.str_val;
                *len = col_res->out_length;
#endif
        }
	} else {
	/* binary numeric form */
#if PHP_8_1_OR_HIGHER
		ZVAL_LONG(result, col_res->data.l_val);
#else
		*ptr = (char *) &col_res->data.l_val;
		*len = col_res->out_length;
#endif
	}

	return TRUE;
}

/* step to the next result set of the query. */
static int informix_stmt_next_rowset(
	pdo_stmt_t *stmt
	)
{
	stmt_handle *stmt_res = (stmt_handle *) stmt->driver_data;

	/*
	* Now get the next result set.  This has the side effect
	* of cleaning up the current cursor, if it exists.
	*/
	int rc = SQLMoreResults(stmt_res->hstmt);
	/*
	* We don't raise errors here.  A success return codes
	* signals we have more result sets to process, so we
	* set everything up to read that info.  Otherwise, we just
	* signal the main driver we're finished.
	*/
	if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
		return FALSE;
	}

	/*
	* The next result set may have different column information, so
	* we need to clear out our existing set.
	*/
	stmt_free_column_descriptors(stmt );
	/* Now allocate a new set of column descriptors */
	if (stmt_allocate_column_descriptors(stmt ) == FALSE) {
		return FALSE;
	}
	/* more results to process */
	return TRUE;
}

/*
* Return all of the meta data information that makes sense for
* this database driver.
*/
static int informix_stmt_get_column_meta(
	pdo_stmt_t *stmt,
	long colno,
	zval *return_value
	)
{
	stmt_handle *stmt_res = NULL;
	column_data *col_res = NULL;

#define ATTRIBUTEBUFFERSIZE 256
	char attribute_buffer[ATTRIBUTEBUFFERSIZE];
	SQLSMALLINT length;
#if PHP_MAJOR_VERSION >= 7
        SQLLEN numericAttribute;
        zval flags;
#else
	SQLINTEGER numericAttribute;
	zval *flags;
#endif

	if (colno >= stmt->column_count) {
		RAISE_INFORMIX_STMT_ERROR("HY097", "getColumnMeta",
			"Column number out of range");
		return FAILURE;
	}

	stmt_res = (stmt_handle *) stmt->driver_data;
	/* access our look aside data */
	col_res = &stmt_res->columns[colno];

	/* make sure the return value is initialized as an array. */
	array_init(return_value);
	add_assoc_long(return_value, "scale", col_res->scale);

	/* see if we can retrieve the table name  */
	if (SQLColAttribute (stmt_res->hstmt, colno + 1, SQL_DESC_BASE_TABLE_NAME,
			(SQLPOINTER) attribute_buffer, ATTRIBUTEBUFFERSIZE, &length,
			(SQLPOINTER) & numericAttribute) != SQL_ERROR) {
		/*
		* Most of the time, this seems to return a null string.  only
		* return this if we have something real.
		*/
		if (length > 0) {
#if PHP_MAJOR_VERSION >= 7
			add_assoc_stringl(return_value, "table", attribute_buffer, length);
#else
			add_assoc_stringl(return_value, "table", attribute_buffer, length, 1);
#endif
		}
	}
	/* see if we can retrieve the type name */
	if (SQLColAttribute(stmt_res->hstmt, colno + 1, SQL_DESC_TYPE_NAME,
			(SQLPOINTER) attribute_buffer, ATTRIBUTEBUFFERSIZE, &length,
			(SQLPOINTER) & numericAttribute) != SQL_ERROR) {
#if PHP_MAJOR_VERSION >= 7
		add_assoc_stringl(return_value, "native_type", attribute_buffer, length);
#else
		add_assoc_stringl(return_value, "native_type", attribute_buffer, length, 1);
#endif
	}

#if PHP_MAJOR_VERSION >= 7
	array_init(&flags);
	add_assoc_bool(&flags, "not_null", !col_res->nullable);
#else
	MAKE_STD_ZVAL(flags);
	array_init(flags);
	add_assoc_bool(flags, "not_null", !col_res->nullable);
#endif

	/* see if we can retrieve the unsigned attribute */
	if (SQLColAttribute(stmt_res->hstmt, colno + 1, SQL_DESC_UNSIGNED,
			(SQLPOINTER) attribute_buffer, ATTRIBUTEBUFFERSIZE, &length,
			(SQLPOINTER) & numericAttribute) != SQL_ERROR) {
#if PHP_MAJOR_VERSION >= 7
		add_assoc_bool(&flags, "unsigned", numericAttribute == SQL_TRUE);
#else
		add_assoc_bool(flags, "unsigned", numericAttribute == SQL_TRUE);
#endif
	}

	/* see if we can retrieve the autoincrement attribute */
	if (SQLColAttribute (stmt_res->hstmt, colno + 1, SQL_DESC_AUTO_UNIQUE_VALUE,
			(SQLPOINTER) attribute_buffer, ATTRIBUTEBUFFERSIZE, &length,
			(SQLPOINTER) & numericAttribute) != SQL_ERROR) {
#if PHP_MAJOR_VERSION >= 7
		add_assoc_bool(&flags, "auto_increment",
		numericAttribute == SQL_TRUE);
#else
		add_assoc_bool(flags, "auto_increment",
		numericAttribute == SQL_TRUE);
#endif
	}

	/* add the flags to the result bundle. */
#if PHP_MAJOR_VERSION >= 7
	add_assoc_zval(return_value, "flags", &flags);
#else
	add_assoc_zval(return_value, "flags", flags);
#endif

#if PHP_8_1_OR_HIGHER
	/*
	 * Just like in stmt_bind_column, but in 8.1, we need to turn it as a
	 * PDO metadata property this time. We can't set it in pdo_column_data.
	 *
	 * XXX: Also safe for pre-8.1?
	 */
	switch (col_res->data_type) {
		/* LOBs */
		case SQL_INFX_UDT_BLOB:
		case SQL_INFX_UDT_CLOB:
			add_assoc_long(return_value, "pdo_type", PDO_PARAM_LOB);
			break;
		/* Strings */
		case SQL_LONGVARCHAR:
		case SQL_CHAR:
		case SQL_VARCHAR:
		case SQL_TYPE_TIME:
		case SQL_TYPE_TIMESTAMP:
		case SQL_BIGINT:
		case SQL_REAL:
		case SQL_FLOAT:
		case SQL_DOUBLE:
		case SQL_DECIMAL:
		case SQL_NUMERIC:
		default:
			add_assoc_long(return_value, "pdo_type", PDO_PARAM_STR);
			break;
		/* XXX: PARAM_(INT|BOOL)? */
	}
#endif
	return SUCCESS;
}

#define CURSOR_NAME_BUFFER_LENGTH 256

/* get driver specific attributes.  We only support CURSOR_NAME. */
static int informix_stmt_get_attribute(
	pdo_stmt_t *stmt,
	long attr,
	zval *return_value
	)
{
	stmt_handle *stmt_res = (stmt_handle *) stmt->driver_data;

	/* anything we can't handle is an error */
	switch (attr) {
		case PDO_ATTR_CURSOR_NAME:
		{
			char buffer[CURSOR_NAME_BUFFER_LENGTH];
			SQLSMALLINT length;

			int rc = SQLGetCursorName(stmt_res->hstmt, buffer,
				CURSOR_NAME_BUFFER_LENGTH, &length);
			check_stmt_error(rc, "SQLGetCursorName");

			/* this is a string value */
#if PHP_MAJOR_VERSION >= 7
			ZVAL_STRINGL(return_value, buffer, length);
#else
			ZVAL_STRINGL(return_value, buffer, length, 1);
#endif
			return TRUE;
		}
		/* unknown attribute */
		default:
		{
			/* raise a driver error, and give the special -1 return. */
			RAISE_INFORMIX_STMT_ERROR("IM001", "getAttribute", "Unknown attribute");
			return -1;
			/* the -1 return does not raise an error immediately. */
		}
	}
}

/* set a driver-specific attribute.  We only support CURSOR_NAME. */
static int informix_stmt_set_attribute(
	pdo_stmt_t *stmt,
	long attr,
	zval *value
	)
{
	stmt_handle *stmt_res = (stmt_handle *) stmt->driver_data;
	int rc = 0;

	switch (attr) {
		case PDO_ATTR_CURSOR_NAME:
		{
			/* we need to force this to a string value */
			convert_to_string(value);
			/* set the cursor value */
			rc = SQLSetCursorName(stmt_res->hstmt, Z_STRVAL_P(value), Z_STRLEN_P(value));
			check_stmt_error(rc, "SQLSetCursorName");
			return TRUE;
		}
		default:
		{
			/* raise a driver error, and give the special -1 return. */
			RAISE_INFORMIX_STMT_ERROR("IM001", "getAttribute", "Unknown attribute");
			return -1;
			/* the -1 return does not raise an error immediately. */
		}
	}
}


int record_last_insert_id(pdo_dbh_t * dbh, SQLHANDLE hstmt )
{
	SQLINTEGER diag_func_type;
	int rc;
	conn_handle *conn_res = (conn_handle *) dbh->driver_data;

	rc = SQLGetDiagField(SQL_HANDLE_STMT, hstmt, 0, SQL_DIAG_DYNAMIC_FUNCTION_CODE, &diag_func_type, 0, NULL);

	if(rc == SQL_ERROR) {
		conn_res->last_insert_id = 0;
		return SQL_ERROR;
	}
	if (diag_func_type == SQL_DIAG_INSERT) {
		rc = SQLGetStmtAttr(hstmt, SQL_GET_SERIAL_VALUE, &conn_res->last_insert_id, SQL_IS_INTEGER, NULL);

		if(rc == SQL_ERROR) {
			conn_res->last_insert_id = 0;
			return SQL_ERROR;
		}
	}
	return TRUE;
}

struct pdo_stmt_methods informix_stmt_methods = {
	informix_stmt_dtor,
	informix_stmt_executer,
	informix_stmt_fetcher,
	informix_stmt_describer,
	informix_stmt_get_col,
	informix_stmt_param_hook,
	informix_stmt_set_attribute,
	informix_stmt_get_attribute,
	informix_stmt_get_column_meta,
	informix_stmt_next_rowset
};
