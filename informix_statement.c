/*
  +----------------------------------------------------------------------+
  | (C) Copyright IBM Corporation 2005.                                  |
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
  | Authors: Rick McGuire, Krishna Raman                                 |
  |                                                                      |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

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

// clear up our column descriptors.  This is done either from 
// the statement constructors or whenever we traverse from one 
// result set to the next. 
static void stmt_free_column_descriptors(pdo_stmt_t *stmt TSRMLS_DC)
{
	stmt_handle *stmt_res = (stmt_handle *)stmt->driver_data;
	if (stmt_res->columns != NULL)
	{
		int i; 
		// see if any of the columns have attached storage too. 
		for (i = 0; i < stmt->column_count; i++)
		{
			// was this a string form?  We have an allocated string 
			// buffer that also needs releasing. 
			if (stmt_res->columns[i].returned_type == PDO_PARAM_STR)
			{
				efree(stmt_res->columns[i].data.str_val);
			}
		}
		
		// free the entire column list. 
		efree(stmt_res->columns);
		stmt_res->columns = NULL;
	}
}

// cleanup any driver-allocated control blocks attached to a statement
// instance.  This cleans up the driver_data control block, as 
// well as any temporary allocations used during execution. 
void stmt_cleanup(pdo_stmt_t *stmt TSRMLS_DC)
{
	stmt_handle *stmt_res = (stmt_handle *)stmt->driver_data;
	if (stmt_res != NULL)
	{
		if (stmt_res->converted_statement != NULL)
		{
			efree(stmt_res->converted_statement);
		}
		if (stmt_res->lob_buffer != NULL)
		{
			efree(stmt_res->lob_buffer);
		}
		// free any descriptors we're keeping active
		stmt_free_column_descriptors(stmt TSRMLS_CC);
		efree(stmt_res);
	}
	stmt->driver_data = NULL;
}

// get the parameter description information for a positional bound parameter. 
static int stmt_get_parameter_info(pdo_stmt_t *stmt, struct pdo_bound_param_data *param TSRMLS_DC)
{
	param_node *param_res = (param_node *)param->driver_data;
	stmt_handle *stmt_res = NULL;
	int rc = 0;

	// do we have the parameter information yet?
	if (param_res == NULL)
	{
		// allocate a new one and attach to the PDO param structure
		param_res = (param_node *)emalloc(sizeof(param_node));
		check_stmt_allocation(param_res, "stmt_get_parameter", "Unable to allocate parameter driver data");

		// get the statement specifics
		stmt_res = (stmt_handle *)stmt->driver_data;
		// NB:  The PDO parameter numbers are origin zero, but the SQLDescribeParam()
		// ones start with 1.
		rc = SQLDescribeParam((SQLHSTMT)stmt_res->hstmt, param->paramno + 1, &param_res->data_type,
			&param_res->param_size, &param_res->scale, &param_res->nullable);
		check_stmt_error(rc, "SQLDescribeParam");

		// only attach this if we succeed
		param->driver_data = param_res;

		// but see if we need to alter this for binary forms or
		// can optimize numerics a little.
		switch (param_res->data_type)
		{
			// the binary forms need to be transferred as binary data,
			// not as char data.
			case SQL_BINARY:
			case SQL_VARBINARY:
			case SQL_LONGVARBINARY:
				param_res->ctype = SQL_C_BINARY;
				break;

			// numeric forms we can map directly to a long
			// int value
			case SQL_SMALLINT:
			case SQL_INTEGER:
				param_res->ctype = SQL_C_LONG;
				break;

			// everything else will transfer as binary
			default:
				// by default, we transfer as character data
				param_res->ctype = SQL_C_CHAR;
				break;
		}
	}
	return TRUE;
}

// bind a statement parameter to the PHP value supplying or receiving the 
// parameter data. 
int stmt_bind_parameter(pdo_stmt_t *stmt, struct pdo_bound_param_data *curr TSRMLS_DC)
{
	int rc;
	stmt_handle *stmt_res = (stmt_handle *)stmt->driver_data;
	param_node *param_res = NULL;
	SQLSMALLINT inputOutputType;

	// make sure we have current description information.
	if (stmt_get_parameter_info(stmt, curr TSRMLS_CC) == FALSE)
	{
		return FALSE;
	}

	param_res = (param_node *)curr->driver_data;

	// now figure out the parameter type so we can tell the database code 
	// how to handle this. 
	// this is rare, really only used for stored procedures. 
	if (curr->param_type & PDO_PARAM_INPUT_OUTPUT > 0)
	{
		inputOutputType = SQL_PARAM_INPUT_OUTPUT;
	}
	// if this is a non-positive length, we can't assign a value,
	// so this is by definition an INPUT param.
	else if (curr->max_value_len <= 0)
	{
		inputOutputType = SQL_PARAM_INPUT;
	}
	// everything else is output.
	else
	{
		inputOutputType = SQL_PARAM_OUTPUT;
	}

	// now do the actual binding, which is controlled by the 
	// PDO supplied type. 
	switch (PDO_PARAM_TYPE(curr->param_type))
	{
		// not implemented yet
		case PDO_PARAM_STMT:
			RAISE_IFX_STMT_ERROR("IM001", "param_hook", "Driver does not support statement parameters");
			return FALSE;

		// this is a long value for PHP
		case PDO_PARAM_INT:
			// if the parameter type is a numeric type, we'll bind this directly,
			if (param_res->ctype == SQL_C_LONG)
			{
				// force this to be a real boolean value
				convert_to_long(curr->parameter);
				rc = SQLBindParameter(stmt_res->hstmt, curr->paramno + 1,
					inputOutputType, SQL_C_LONG, param_res->data_type, param_res->param_size,
					param_res->scale, &((curr->parameter)->value.lval), 0, NULL);
				check_stmt_error(rc, "SQLBindParameter");
				return TRUE;
			}
			
		// NOTE:  We fall through from above if there is a type mismatch.

		// a string value (very common)
		case PDO_PARAM_BOOL:
		case PDO_PARAM_STR:
			// if we're capable of handling an integer value, but PDO 
			// is telling us string, then change this now. 
			if (param_res->ctype == SQL_C_LONG)
			{
				// change this to a character type
				param_res->ctype = SQL_C_CHAR;
			}
			if( Z_TYPE_P(curr->parameter) == IS_NULL ){
				param_res->ctype = SQL_C_DEFAULT;
				param_res->param_size = 0;
				param_res->scale = 0;
				curr->max_value_len = 0;
				param_res->transfer_length = SQL_NULL_DATA;
				rc = SQLBindParameter(stmt_res->hstmt, curr->paramno + 1,
					inputOutputType, param_res->ctype, param_res->data_type, param_res->param_size,
					param_res->scale, NULL , curr->max_value_len <= 0 ? 0 : curr->max_value_len,
					&param_res->transfer_length);
				check_stmt_error(rc, "SQLBindParameter");
			}else{
				// force this to be a real string value
				convert_to_string(curr->parameter);
				// set the transfer length to zero now...this gets update
				// at EXEC_PRE time.
				param_res->transfer_length = 0;

				// now we need to make sure the string buffer is large enough
				// to receive a new value if this is an output or in/out parameter
				if (inputOutputType != SQL_PARAM_INPUT && curr->max_value_len > Z_STRLEN_P(curr->parameter))
				{
					// reallocate this to the new size
					Z_STRVAL_P(curr->parameter) = erealloc(Z_STRVAL_P(curr->parameter), curr->max_value_len + 1);
					check_stmt_allocation(Z_STRVAL_P(curr->parameter), "stmt_bind_parameter", "Unable to allocate bound parameter");
				}
				rc = SQLBindParameter(stmt_res->hstmt, curr->paramno + 1,
						inputOutputType, param_res->ctype, param_res->data_type, param_res->param_size,
						param_res->scale, Z_STRVAL_P(curr->parameter), curr->max_value_len <= 0 ? 0 : curr->max_value_len,
						&param_res->transfer_length);
				check_stmt_error(rc, "SQLBindParameter");
			}

			return TRUE;

		// this is either a string, or, if the length is zero,
		// then this is a pointer to a PHP stream.
		case PDO_PARAM_LOB:
			if (inputOutputType != SQL_PARAM_INPUT)
			{
				inputOutputType = SQL_PARAM_INPUT;
				//RAISE_IFX_STMT_ERROR( "HY105" , "SQLBindParameter" , "PDO_PARAM_LOB parameters can only be used for input" );
				//return FALSE;
			}

			// have we bound a LOB to a long type for some reason?
			if (param_res->ctype == SQL_C_LONG)
			{
				// transfer this as character data.
				param_res->ctype = SQL_C_CHAR;
			}

			// indicate we're going to transfer the data at exec time.
			param_res->transfer_length = SQL_DATA_AT_EXEC;

			// we can't bind LOBs at this point...we process all of this at execute time.
			// however, we set the value data to the PDO binding control block and
			// set the SQL_DATA_AT_EXEC value to cause it to prompt us for the data at
			// execute time.  The pointer is recoverable at that time by using
			// SQLParamData(), and we can then process the request.
			rc = SQLBindParameter(stmt_res->hstmt, curr->paramno + 1,
				inputOutputType, param_res->ctype, param_res->data_type, param_res->param_size,
				param_res->scale, curr, 0, &param_res->transfer_length);
			check_stmt_error(rc, "SQLBindParameter");
			return TRUE;

		// this is an unknown type
		default:
			RAISE_IFX_STMT_ERROR( "IM001" , "SQLBindParameter" , "Unknown parameter type" );
			return FALSE;
	}

	return TRUE;
}

// handle the pre-execution phase for bound parameters. 
static int stmt_parameter_pre_execute(pdo_stmt_t *stmt, struct pdo_bound_param_data *curr TSRMLS_DC)
{
	stmt_handle *stmt_res = (stmt_handle *)stmt->driver_data;
	param_node *param_res = (param_node *)curr->driver_data;

	// now we need to prepare the parameter binding information
	// for execution.  If this is a LOB, then we need to ensure
	// the LOB data is going to be available and make sure
	// the binding is tagged to provide the data at exec time.
	if (PDO_PARAM_TYPE(curr->param_type) == PDO_PARAM_LOB)
	{
		// if the LOB data is a stream, we need to make sure it is
		// really there.
		if (Z_TYPE_P(curr->parameter) == IS_RESOURCE)
		{
			php_stream *stm;
			php_stream_statbuf sb;

			// make sure we have a stream to work with
			php_stream_from_zval_no_verify(stm, &curr->parameter);

			if (stm == NULL)
			{
				RAISE_IFX_STMT_ERROR( "HY000" , "SQLBindParameter" , "PDO_PARAM_LOB file stream is invalid");
			}

			// now see if we can retrieve length information from the stream
			if (php_stream_stat(stm, &sb) == 0)
			{
				// yes, we're able to give the statement some hints about the size.
				param_res->transfer_length = SQL_LEN_DATA_AT_EXEC(sb.sb.st_size);
			}
			else
			{
				// still unknown...we'll have to do everything at execute size.
				param_res->transfer_length = SQL_LEN_DATA_AT_EXEC(0);
			}
		}
		else
		{
			// convert this to a string value now.  We bound the data pointer to our
			// parameter descriptor, so we can't just supply this directly yet, but we
			// can at least give the size hint information.
			convert_to_string(curr->parameter);
			param_res->transfer_length = SQL_LEN_DATA_AT_EXEC(Z_STRLEN_P(curr->parameter));
		}

	}
	else
	{
		if( Z_TYPE_P(curr->parameter) != IS_NULL ){
			// if we're processing this as string or binary data, then
			// directly update the length to the real value.
			if (param_res->ctype == SQL_C_LONG)
			{
				// make sure this is a long value
				convert_to_long(curr->parameter);
			}
			else
			{
				// make sure this is a string value...it might have been
				// changed between the bind and the execute
				convert_to_string(curr->parameter);
				param_res->transfer_length = Z_STRLEN_P(curr->parameter);
			}
		}
	}
	return TRUE;
}

// post-execution bound parameter handling. 
static int stmt_parameter_post_execute(pdo_stmt_t *stmt, struct pdo_bound_param_data *curr TSRMLS_DC)
{
	param_node *param_res = (param_node *)curr->driver_data;

	// if the type of the parameter is a string, we need to update the
	// string length and make sure that these are null terminated.
	// Values returned from the DB are just copied directly into the bound 
	// locations, so we need to update the PHP control blocks so that the 
	// data is processed correctly. 
	if (Z_TYPE_P(curr->parameter) == IS_STRING)
	{ 
		if( param_res->transfer_length == SQL_NULL_DATA ){
			ZVAL_NULL(curr->parameter);
		}else if(param_res->transfer_length == 0){
			ZVAL_EMPTY_STRING(curr->parameter);
		}else{
			Z_STRLEN_P(curr->parameter) = param_res->transfer_length;
			Z_STRVAL_P(curr->parameter)[param_res->transfer_length] = '\0';
		}
	}
	return TRUE;
}

// bind a column to an internally allocated buffer location. 
static int stmt_bind_column(pdo_stmt_t *stmt, int colno TSRMLS_DC)
{
	stmt_handle *stmt_res = (stmt_handle *)stmt->driver_data;
	// access the information for this column
	column_data *col_res = &stmt_res->columns[colno];
	struct pdo_column_data *col = NULL;
	char tmp_name[BUFSIZ];

	// get the column descriptor information 
	int rc = SQLDescribeCol((SQLHSTMT)stmt_res->hstmt, (SQLSMALLINT)(colno + 1 ),
		tmp_name, BUFSIZ, &col_res->namelen, &col_res->data_type, &col_res->data_size, &col_res->scale, &col_res->nullable);
	check_stmt_error(rc, "SQLDescribeCol");

	// make sure we get a name properly.  If the name is too long for our 
	// buffer (which in theory should never happen), allocate a longer one 
	// and ask for the information again. 
	if (col_res->namelen <= 0 )
	{
		col_res->name = estrdup("");
		check_stmt_allocation(col_res->name, "stmt_bind_column", "Unable to allocate column name");
	}
	else if (col_res->namelen >= BUFSIZ )
	{
		/* column name is longer than BUFSIZ*/
		col_res->name = emalloc(col_res->namelen + 1);
		check_stmt_allocation(col_res->name, "stmt_bind_column", "Unable to allocate column name");
		rc = SQLDescribeCol((SQLHSTMT)stmt_res->hstmt, (SQLSMALLINT)(colno + 1 ),
			col_res->name, BUFSIZ, &col_res->namelen, &col_res->data_type, 
			&col_res->data_size, &col_res->scale, &col_res->nullable);
		check_stmt_error(rc, "SQLDescribeCol");
	}
	else
	{
		col_res->name = estrdup(tmp_name);
		check_stmt_allocation(col_res->name, "stmt_bind_column", "Unable to allocate column name");
	}

	col = &stmt->columns[colno];

	// copy the information back into the PDO control block.  Note that 
	// PDO will release the name information, so we don't have to. 
	col->name = col_res->name;
	col->namelen = col_res->namelen;
	col->maxlen = col_res->data_size;
	col->precision = col_res->scale;

	// now process the column types
	switch (col_res->data_type)
	{
// the PDO test cases are written to expect everything to be returned 
// as a string value.  The following code works, but it forces the values 
// to PDO ints, and the test driver doesn't like that. 		
#if 0		
		// this is a long value for PHP
		case SQL_SMALLINT:
		case SQL_INTEGER:
			rc = SQLBindCol((SQLHSTMT)stmt_res->hstmt, (SQLUSMALLINT)(colno + 1),
				SQL_C_LONG, &col_res->data.l_val, sizeof(col_res->data.l_val),
				(SQLINTEGER *)(&col_res->out_length));
			// these are all returned as long values
			col_res->returned_type = PDO_PARAM_INT;
			col->param_type = PDO_PARAM_INT;
			break;
#endif		

		// a form we need to force into a string value...this includes any unknown types
		case SQL_CHAR:
		case SQL_VARCHAR:
		case SQL_LONGVARCHAR:
		case SQL_TYPE_TIME:
		case SQL_TYPE_TIMESTAMP:
		case SQL_BIGINT:
		case SQL_REAL:
		case SQL_FLOAT:
		case SQL_DOUBLE:
		case SQL_DECIMAL:
		case SQL_NUMERIC:
		default:
		{
			int in_length = col_res->data_size + 1;
			col_res->data.str_val = (char *)emalloc(in_length);
			check_stmt_allocation(col_res->data.str_val, "stmt_bind_column", "Unable to allocate column buffer");
			rc = SQLBindCol((SQLHSTMT)stmt_res->hstmt, (SQLUSMALLINT)(colno + 1),
				SQL_C_CHAR, col_res->data.str_val, in_length,
				(SQLINTEGER *)(&col_res->out_length));
			col_res->returned_type = PDO_PARAM_STR;
			col->param_type = PDO_PARAM_STR; 
		}
#if 0		
		// a blob form....we'll process this at fetch time by doing
		// a SQLGetData() call.
		case SQL_CLOB:
		case SQL_BLOB:
			// we're going to need to do getdata calls to retrieve these
			col_res->out_length = 0;
			// and this is returned as a string too
			col_res->returned_type = PDO_PARAM_STR;
			break;
#endif 		
	}
	return TRUE;
}

// allocate a set of internal column descriptors for a statement. 
static int stmt_allocate_column_descriptors(pdo_stmt_t *stmt TSRMLS_DC)
{
	stmt_handle *stmt_res = (stmt_handle *)stmt->driver_data;
	SQLSMALLINT nResultCols = 0;

	// not sure
	int rc = SQLNumResultCols((SQLHSTMT)stmt_res->hstmt, &nResultCols);
	check_stmt_error(rc, "SQLNumResultCols");

	// make sure we set the count in the PDO stmt structure so the driver
	// knows how many columns we're dealing with.
	stmt->column_count = nResultCols;

	// allocate the column descriptors now.  We'll bind each column individually before the
	// first fetch.  The binding process will allocate any additional buffers we might need
	// for the data.
	stmt_res->columns = (column_data *)ecalloc(sizeof(column_data), stmt->column_count);
	check_stmt_allocation(stmt_res->columns, "stmt_allocate_column_descriptors", "Unable to allocate column descriptor tables");
	memset(stmt_res->columns, '\0', sizeof(column_data) * stmt->column_count);
	return TRUE;
}

// this is also used for error cleanup for errors that occur while the stmt is still
// half constructed.
int informix_stmt_dtor( pdo_stmt_t *stmt TSRMLS_DC)
{
	stmt_handle *stmt_res = (stmt_handle *)stmt->driver_data;

	if (stmt_res != NULL)
	{
		if (stmt_res->hstmt != SQL_NULL_HANDLE)
		{
			// if we've done some work, we need to clean up. 
			if (stmt->executed)
			{	
				// cancel anything we have pending at this point
				SQLCancel(stmt_res->hstmt);
			}	
			SQLFreeHandle(SQL_HANDLE_STMT, stmt_res->hstmt);
			stmt_res->hstmt = SQL_NULL_HANDLE;
		}
		// release an control blocks we have attached to this statement
		stmt_cleanup(stmt TSRMLS_CC);
	}
	return TRUE;
}

// execute a PDOStatement.  Used for both the PDOStatement::execute() method 
// as well as the PDO:query() method. 
static int informix_stmt_executer(
	pdo_stmt_t *stmt
	TSRMLS_DC)
{
	stmt_handle *stmt_res = (stmt_handle *)stmt->driver_data;
	int rc = 0;
	SQLINTEGER rowCount;

	// if this statement has already been executed, then we need to 
	// cancel the previous execution before doing this again. 
	if (stmt->executed) 
	{
		rc = SQLFreeStmt( stmt_res->hstmt , SQL_CLOSE );
		check_stmt_error(rc, "SQLFreeStmt");
	}	
	// we're executing now...this tells error handling to Cancel if there's an error.
	stmt_res->executing = 1;


	stmt_res->lob_buffer = NULL; 
	// Execute the statement.  All parameters should be bound at this point, 
	// but we might need to pump data in for some of the parameters. 
	rc = SQLExecute((SQLHSTMT)stmt_res->hstmt);
	check_stmt_error(rc, "SQLExecute");
	// now check if we have indirectly bound parameters.  If we do, then we 
	// need to push the data for those parameters into the processing pipe. 
	while (rc == SQL_NEED_DATA)
	{
		struct pdo_bound_param_data *param;
		// get the associated parameter data.  The bind process should have 
		// stored a pointer to the parameter control block, so we identify 
		// which one needs data from that.     
		rc = SQLParamData(stmt_res->hstmt, (SQLPOINTER *)&param);
		check_stmt_error(rc, "SQLParamData");
		if (rc == SQL_NEED_DATA)
		{
			// OK, we have a LOB.  This is either in string form, in which case we can supply it
			// directly, or is a PHP stream.  If it is a stream, then the type is IS_RESOURCE, and
			// we need to pump the data in a buffer at a time.
			if (Z_TYPE_P(param->parameter) != IS_RESOURCE)
			{
				rc = SQLPutData(stmt_res->hstmt, Z_STRVAL_P(param->parameter), Z_STRLEN_P(param->parameter));
				check_stmt_error(rc, "SQLPutData");
			}
			else
			{
				// the LOB is a stream.  This better still be good, else we 
				// can't supply the data. 
				php_stream *stm = NULL;
				int len;
				php_stream_from_zval_no_verify(stm, &(param->parameter));
				if (!stm)
				{
					RAISE_IFX_STMT_ERROR("HY000", "execute", "Input parameter LOB is no longer a valid stream");
					return FALSE;
				}
				// allocate a buffer if we haven't prior to this
				if (stmt_res->lob_buffer == NULL)
				{
					stmt_res->lob_buffer = emalloc(LOB_BUFFER_SIZE);
					check_stmt_allocation(stmt_res->lob_buffer, "stmt_execute", "Unable to allocate parameter data buffer");
				}
				
				// read a buffer at a time and push into the execution pipe. 
				for (;;)
				{
					len = php_stream_read(stm, stmt_res->lob_buffer, LOB_BUFFER_SIZE);
					if (len == 0)
					{
						break;
					}
					// add the buffer
					rc = SQLPutData(stmt_res->hstmt, stmt_res->lob_buffer, len);
					check_stmt_error(rc, "SQLPutData");
				}
			}

			rc = SQLParamData(stmt_res->hstmt, (SQLPOINTER *)&param);
			check_stmt_error(rc, "SQLParamData");
		}
	}

	// free any LOB buffer we might have
	if (stmt_res->lob_buffer != NULL)
	{
		efree(stmt_res->lob_buffer);
	}

	// now set the rowcount field in the statement.  This will be the 
	// number of rows affected by the SQL statement, not the number of 
	// rows in the result set. 
	rc = SQLRowCount(stmt_res->hstmt, &rowCount);
	check_stmt_error(rc, "SQLRowCount");
	// store the affected rows information. 
	stmt->row_count = rowCount;
	
	// is this the first time we've executed this statement?
	if (!stmt->executed)
	{
		if (stmt_allocate_column_descriptors(stmt TSRMLS_CC) == FALSE)
		{
			return FALSE;
		}
	}

	// we can turn off the cleanup flag now
	stmt_res->executing = 0;

	return TRUE;
}

// fetch the next row of the result set. 
static int informix_stmt_fetcher(
	pdo_stmt_t *stmt,
	enum pdo_fetch_orientation ori,
	long offset
	TSRMLS_DC)
{
	stmt_handle *stmt_res = (stmt_handle *)stmt->driver_data;
	// by default, we're just fetching the next one
	SQLSMALLINT direction = SQL_FETCH_NEXT;
	int rc = 0;

	// convert the PDO orientation information to the 
	// SQL one. 
	switch (ori)
	{
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

	// go fetch it. 
	rc = SQLFetchScroll(stmt_res->hstmt, direction, (SQLINTEGER)offset);
	check_stmt_error(rc, "SQLFetchScroll");

	// The fetcher() routine has an overloaded return value.  A false return 
	// can indicate either an error or the end of the row data. 
	if (rc == SQL_NO_DATA) 
	{
		// if we are scrolling forward, then close the cursor to release 
		// resources tied up by this statement. 
		if (stmt_res->cursor_type == PDO_CURSOR_FWDONLY)
		{
			SQLCloseCursor(stmt_res->hstmt); 
		}
		return FALSE;
	}
	return TRUE;
}	

// process the various bound parameter events. 
static int informix_stmt_param_hook(
	pdo_stmt_t *stmt,
	struct pdo_bound_param_data *param,
	enum pdo_param_event event_type
	TSRMLS_DC)
{
	// we get called for both parameters and bound columns.  We
	// only need to process the parameters
	if (param->is_param)
	{
		switch (event_type)
		{
			case PDO_PARAM_EVT_ALLOC:
				break;

			case PDO_PARAM_EVT_FREE:
				// during the alloc event, we attached some 
				// driver specific data.  We need to free this now. 
				if (param->driver_data != NULL)
				{
					efree(param->driver_data);
					param->driver_data = NULL;
				}
				break;

			case PDO_PARAM_EVT_EXEC_PRE:
				// we're allocating a bound parameter, go do the binding
				stmt_bind_parameter(stmt, param TSRMLS_CC);
				 return stmt_parameter_pre_execute(stmt, param TSRMLS_CC);

			case PDO_PARAM_EVT_EXEC_POST:
				return stmt_parameter_post_execute(stmt, param TSRMLS_CC);

			// parameters aren't processed at the fetch phase.
			case PDO_PARAM_EVT_FETCH_PRE:
			case PDO_PARAM_EVT_FETCH_POST:
				break;
		}
	}
	return TRUE;
}	

// describe a column for the PDO driver. 
static int informix_stmt_describer(
	pdo_stmt_t *stmt,
	int colno
	TSRMLS_DC)
{
	// get the descriptor information and bind to the column location
	return stmt_bind_column(stmt, colno TSRMLS_CC);
}

// fetch the data for a specific column.  This should be sitting in our 
// allocated buffer already, and easy to return. 
static int informix_stmt_get_col(
	pdo_stmt_t *stmt,
	int colno,
	char **ptr,
	unsigned long *len,
	int *caller_frees
	TSRMLS_DC)
{
	
	stmt_handle *stmt_res = (stmt_handle *)stmt->driver_data;
	// access our look aside data
	column_data *col_res = &stmt_res->columns[colno];

	// see if this is a null value
	if (col_res->out_length == SQL_NULL_DATA)
	{
		// return this as a real null
		*ptr = NULL;
		*len = 0;
	}
	// string type...very common
	else if (col_res->returned_type == PDO_PARAM_STR)
	{
		// set the info
		*ptr = col_res->data.str_val;
		*len = col_res->out_length;
	}
	// binary numeric form
	else
	{
		*ptr = (char *)&col_res->data.l_val;
		*len = col_res->out_length;
	}

	return TRUE;
}

// step to the next result set of the query. 
static int informix_stmt_next_rowset(
	pdo_stmt_t *stmt
	TSRMLS_DC)
{
	stmt_handle *stmt_res = (stmt_handle *)stmt->driver_data;

	// now get the next result set.  This has the side effect 
	// of cleaning up the current cursor, if it exists.  
	int rc = SQLMoreResults(stmt_res->hstmt); 
	// we don't raise errors here.  A success return codes 
	// signals we have more result sets to process, so we 
	// set everything up to read that info.  Otherwise, we just 
	// signal the main driver we're finished. 
	if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) 
	{
		return FALSE; 
	}

	// the next result set may have different column information, so 
	// we need to clear out our existing set. 
	stmt_free_column_descriptors(stmt TSRMLS_CC); 
	// now allocate a new set of column descriptors
	if (stmt_allocate_column_descriptors(stmt TSRMLS_CC) == FALSE)
	{
		return FALSE;
	}
	// more results to process
	return TRUE;               
}	

// return all of the meta data information that makes sense for 
// this database driver. 
static int informix_stmt_get_column_meta(
	pdo_stmt_t *stmt, 
	long colno, 
	zval *return_value 
	TSRMLS_DC)
{
	stmt_handle *stmt_res = NULL;
	column_data *col_res = NULL;

	#define ATTRIBUTEBUFFERSIZE 256 
	char attribute_buffer[ATTRIBUTEBUFFERSIZE]; 
	SQLSMALLINT length; 
	SQLINTEGER numericAttribute; 
	zval *flags;

	if (colno >= stmt->column_count) 
	{
		RAISE_IFX_STMT_ERROR("HY097", "getColumnMeta", "Column number out of range"); 
		return FAILURE; 
	}        
	
	stmt_res = (stmt_handle *)stmt->driver_data;
	// access our look aside data
	col_res = &stmt_res->columns[colno];
	
		// make sure the return value is initialized as an array. 
	array_init(return_value);  
	add_assoc_long(return_value, "scale", col_res->scale); 
		
	// see if we can retrieve the table name 
	if (SQLColAttribute(stmt_res->hstmt, colno + 1, SQL_DESC_BASE_TABLE_NAME, 
		(SQLPOINTER)attribute_buffer, ATTRIBUTEBUFFERSIZE, &length, 
		(SQLPOINTER)&numericAttribute) != SQL_ERROR) 
	{
		// most of the time, this seems to return a null string.  only 
		// return this if we have something real. 
		if (length > 0) 
		{    
			add_assoc_stringl(return_value, "table", attribute_buffer, length, 1); 
		}
	}    
// see if we can retrieve the type name 
	if (SQLColAttribute(stmt_res->hstmt, colno + 1, SQL_DESC_TYPE_NAME, 
		(SQLPOINTER)attribute_buffer, ATTRIBUTEBUFFERSIZE, &length, 
		(SQLPOINTER)&numericAttribute) != SQL_ERROR) 
	{
		add_assoc_stringl(return_value, "native_type", attribute_buffer, length, 1); 
	}        
	 
	MAKE_STD_ZVAL(flags); 
	array_init(flags); 
	
	add_assoc_bool(flags, "not_null", !col_res->nullable);
	
// see if we can retrieve the unsigned attribute 
	if (SQLColAttribute(stmt_res->hstmt, colno + 1, SQL_DESC_UNSIGNED, 
		(SQLPOINTER)attribute_buffer, ATTRIBUTEBUFFERSIZE, &length, 
		(SQLPOINTER)&numericAttribute) != SQL_ERROR) 
	{
		add_assoc_bool(flags, "unsigned", numericAttribute == SQL_TRUE); 
	}          
	
// see if we can retrieve the autoincrement attribute 
	if (SQLColAttribute(stmt_res->hstmt, colno + 1, SQL_DESC_AUTO_UNIQUE_VALUE, 
		(SQLPOINTER)attribute_buffer, ATTRIBUTEBUFFERSIZE, &length, 
		(SQLPOINTER)&numericAttribute) != SQL_ERROR) 
	{
		add_assoc_bool(flags, "auto_increment", numericAttribute == SQL_TRUE); 
	}          
		
	
	// add the flags to the result bundle. 
	add_assoc_zval(return_value, "flags", flags); 
	
	return SUCCESS;
}

#define CURSOR_NAME_BUFFER_LENGTH 256

// get driver specific attributes.  We only support CURSOR_NAME. 
static int informix_stmt_get_attribute(
	pdo_stmt_t *stmt, 
	long attr, 
	zval *return_value
	TSRMLS_DC)
{
	stmt_handle *stmt_res = (stmt_handle *)stmt->driver_data;
	
	// anything we can't handle is an error
	switch (attr) 
	{
		case PDO_ATTR_CURSOR_NAME:
		{
			char buffer[CURSOR_NAME_BUFFER_LENGTH]; 
			SQLSMALLINT length;
			
			int rc = SQLGetCursorName(stmt_res->hstmt, buffer, 
				CURSOR_NAME_BUFFER_LENGTH, &length);
			check_stmt_error(rc, "SQLGetCursorName");
			
			// this is a string value 
			ZVAL_STRINGL(return_value, buffer, length, 1); 
			return TRUE;    
		}  
		// unknown attribute
		default:
		{
			// raise a driver error, and give the special -1 return. 
			RAISE_IFX_STMT_ERROR("IM001", "getAttribute", "Unknown attribute"); 
			return -1;    // the -1 return does not raise an error immediately.
		}
	}
}

// set a driver-specific attribute.  We only support CURSOR_NAME. 
static int informix_stmt_set_attribute(
	pdo_stmt_t *stmt, 
	long attr, 
	zval *value
	TSRMLS_DC)
{
	stmt_handle *stmt_res = (stmt_handle *)stmt->driver_data;
	int rc = 0;

	switch (attr) 
	{
		case PDO_ATTR_CURSOR_NAME:
		{
			// we need to force this to a string value    
			convert_to_string(value);   
			// set the cursor value
			rc = SQLSetCursorName(stmt_res->hstmt, 
				Z_STRVAL_P(value), Z_STRLEN_P(value)); 
			check_stmt_error(rc, "SQLSetCursorName"); 
			return TRUE;
		}
		default:
		{
			// raise a driver error, and give the special -1 return. 
			RAISE_IFX_STMT_ERROR("IM001", "getAttribute", "Unknown attribute"); 
			return -1;    // the -1 return does not raise an error immediately.
		}
	}
}

struct pdo_stmt_methods informix_stmt_methods =
{ 
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
