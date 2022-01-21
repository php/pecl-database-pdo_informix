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
#include "zend_exceptions.h"
#include <stdio.h>

extern struct pdo_stmt_methods informix_stmt_methods;
extern int informix_stmt_dtor(pdo_stmt_t *stmt );


/* allocate and initialize the driver_data portion of a PDOStatement object. */
static int dbh_new_stmt_data(pdo_dbh_t* dbh, pdo_stmt_t *stmt )
{
	conn_handle *conn_res = (conn_handle *) dbh->driver_data;

	stmt_handle *stmt_res = (stmt_handle *) emalloc(sizeof(stmt_handle));
	check_allocation(stmt_res, "dbh_new_stmt_data", "Unable to allocate stmt driver data");
	memset(stmt_res, '\0', sizeof(stmt_handle));

	stmt_res->columns = NULL;

	/* attach to the statement */
	stmt->driver_data = stmt_res;
	stmt->supports_placeholders = PDO_PLACEHOLDER_POSITIONAL;
	return TRUE;
}

/* prepare a statement for execution. */
#if PHP_8_1_OR_HIGHER
static int dbh_prepare_stmt(pdo_dbh_t *dbh, pdo_stmt_t *stmt, zend_string *stmt_string, zval *driver_options)
#else
static int dbh_prepare_stmt(pdo_dbh_t *dbh, pdo_stmt_t *stmt, const char *stmt_string, long stmt_len, zval *driver_options )
#endif
{
	conn_handle *conn_res = (conn_handle *) dbh->driver_data;
	stmt_handle *stmt_res = (stmt_handle *) stmt->driver_data;
	int rc;
	SQLSMALLINT param_count;
	UCHAR server_info[30];
	SQLSMALLINT server_len = 0;

	/* in case we need to convert the statement for positional syntax */
#if !PHP_8_1_OR_HIGHER
	size_t converted_len = 0;
#endif
	stmt_res->converted_statement = NULL;

	/* clear the current error information to get ready for new execute */
	clear_stmt_error(stmt);

	/*
	 * the statement passed in to us at this point is the raw statement the
	 *  programmer specified.  If the statement is using named parameters
	 * (e.g., ":salary", we can't process this directly.  Fortunately, PDO
	 * has a utility function that will munge the SQL statement into the
	 * form we require and do mappings from named to positional parameters.
	 */

	/* this is necessary...it tells the parser what we require */
	stmt->supports_placeholders = PDO_PLACEHOLDER_POSITIONAL;
#if PHP_8_1_OR_HIGHER
        rc = pdo_parse_params(stmt, (char *) stmt_string,
                              &stmt_res->converted_statement);
#else
	rc = pdo_parse_params(stmt, (char *) stmt_string, stmt_len,
			&stmt_res->converted_statement,
			&converted_len );
#endif

	/*
	 * If the query needed reformatting, a new statement string has been
	 * passed back to us.  We're responsible for freeing this when we're done.
	 */
	if (rc == 1) {
		stmt_string = stmt_res->converted_statement;
#if !PHP_8_1_OR_HIGHER
		stmt_len = converted_len;
#endif
	}
	/*
	 * A negative return indicates there was an error.  The error_code
	 * information in the statement contains the reason.
	 */
	else if (rc == -1) {
		/* copy the error information */
		RAISE_INFORMIX_STMT_ERROR(stmt->error_code, "pdo_parse_params", 
			"Invalid SQL statement");
		/* this failed...error cleanup will happen later. */
		return FALSE;
	}

	/* alloc handle and return only if it errors */
	rc = SQLAllocHandle(SQL_HANDLE_STMT, conn_res->hdbc, &(stmt_res->hstmt));
	check_stmt_error(rc, "SQLAllocHandle");

	/* now see if the cursor type has been explicitly specified. */
	stmt_res->cursor_type = pdo_attr_lval(driver_options, PDO_ATTR_CURSOR, 
			PDO_CURSOR_FWDONLY );

	/*
	 * The default is just sequential access.  If something else has been
	 * specified, we need to make this scrollable.
	 */
	if (stmt_res->cursor_type != PDO_CURSOR_FWDONLY) {
		/* set the statement attribute */
		rc = SQLSetStmtAttr(stmt_res->hstmt, SQL_ATTR_CURSOR_TYPE, (void *) SQL_CURSOR_DYNAMIC, 0);
		check_stmt_error(rc, "SQLSetStmtAttr");
	}


	/* Prepare the stmt. */
#if PHP_8_1_OR_HIGHER
	rc = SQLPrepare((SQLHSTMT) stmt_res->hstmt, (SQLCHAR *) ZSTR_VAL(stmt_string), ZSTR_LEN(stmt_string));
#else
	rc = SQLPrepare((SQLHSTMT) stmt_res->hstmt, (SQLCHAR *) stmt_string, stmt_len);
#endif

	/* Check for errors from that prepare */
	check_stmt_error(rc, "SQLPrepare");
	if (rc == SQL_ERROR) {
		stmt_cleanup(stmt );
		return FALSE;
	}

	/* we can get rid of the stmt copy now */
	if (stmt_res->converted_statement != NULL) {
		efree(stmt_res->converted_statement);
		stmt_res->converted_statement = NULL;
	}

	rc = SQLNumResultCols((SQLHSTMT) stmt_res->hstmt, (SQLSMALLINT *) & param_count);
	check_stmt_error(rc, "SQLNumResultCols");

	/* we're responsible for setting the column_count for the PDO driver. */
	stmt->column_count = param_count;

	/* Get the server information:
	 * server_info is in this form:
	 * 0r.01.0000
	 * where r is the major version
	 */
	rc = SQLGetInfo(conn_res->hdbc, SQL_DBMS_VER, &server_info,
			sizeof(server_info), &server_len);
	/* making char numbers into integers eg. "10" --> 10 or "09" --> 9 */
	stmt_res->server_ver = ((server_info[0] - '0')*100) + ((server_info[1] - '0')*10) + (server_info[3] - '0');
	/*
	 * Attach the methods...we are now live, so errors will no longer immediately
	 * force cleanup of the stmt driver-specific storage.
	 */
	stmt->methods = &informix_stmt_methods;

	return TRUE;
}

/* debugging routine for printing out failure information. */
static void current_error_state(pdo_dbh_t *dbh)
{
	conn_handle *conn_res = (conn_handle *)dbh->driver_data;
	printf("Handling error %s (%s[%d] at %s:%d)\n",
		conn_res->error_data.err_msg,		/* an associated message */
		conn_res->error_data.failure_name,	/* the routine name */
		conn_res->error_data.sqlcode,		/* native error code of the failure */
		conn_res->error_data.filename,		/* source file of the reported error */
		conn_res->error_data.lineno);		/* location of the reported error */
}

/*
*  NB.  The handle closer is used for PDO dtor purposes, but we also use this
*  for error cleanup if we need to throw an exception while creating the
*  connection.  In that case, the closer is not automatically called by PDO,
*  so we need to force cleanup.
*/
static NO_STATUS_RETURN_TYPE informix_handle_closer( pdo_dbh_t * dbh )
{
	conn_handle *conn_res;

	conn_res = (conn_handle *) dbh->driver_data;

	/*
	 * An error can occur at many stages of setup, so we need to check the
	 * validity of each bit as we unwind.
	 */
	if (conn_res != NULL) {
		/* did we get at least as far as creating the environment? */
		if (conn_res->henv != SQL_NULL_HANDLE) {
			/*
			* If we have a handle for the connection, we have
			* more stuff to clean up
			*/
			if (conn_res->hdbc != SQL_NULL_HANDLE) {
				/*
				* Roll back the transaction if this hasn't been committed yet.
				* There's no point in checking for errors here...
				* PDO won't process any of the failures even if they happen.
				*/
				if (dbh->auto_commit == 0) {
					SQLEndTran(SQL_HANDLE_DBC, (SQLHDBC) conn_res->hdbc, 
							SQL_ROLLBACK);
				}
				SQLDisconnect((SQLHDBC) conn_res->hdbc);
				SQLFreeHandle(SQL_HANDLE_DBC, conn_res->hdbc);
			}
			/* and finally the handle */
			SQLFreeHandle(SQL_HANDLE_ENV, conn_res->henv);
		}
		/* now free the driver data */
		pefree(conn_res, dbh->is_persistent);
		dbh->driver_data = NULL;
	}
#if !PHP_8_1_OR_HIGHER
	return TRUE;
#endif
}

/* prepare a statement for execution. */
static STATUS_RETURN_TYPE informix_handle_preparer(
	pdo_dbh_t *dbh,
#if PHP_8_1_OR_HIGHER
	zend_string *sql,
#else
	const char *sql,
	size_t sql_len,
#endif
	pdo_stmt_t *stmt,
	zval *driver_options
	)
{
	conn_handle *conn_res = (conn_handle *)dbh->driver_data;

	/* allocate new driver_data structure */
	if (dbh_new_stmt_data(dbh, stmt ) == TRUE) {
		/* Allocates the stmt handle */
		/* Prepares the statement */
		/* returns the stat_handle back to the calling function */
#if PHP_8_1_OR_HIGHER
		return dbh_prepare_stmt(dbh, stmt, sql, driver_options);
#else
		return dbh_prepare_stmt(dbh, stmt, sql, sql_len, driver_options );
#endif
	}
	return FALSE;
}

/* directly execute an SQL statement. */
#if PHP_8_1_OR_HIGHER
static zend_long informix_handle_doer(
        pdo_dbh_t *dbh,
	const zend_string *sql)
#else
static long informix_handle_doer(
	pdo_dbh_t *dbh,
	const char *sql,
	size_t sql_len)
#endif	
{
	conn_handle *conn_res = (conn_handle *) dbh->driver_data;
	SQLHANDLE hstmt;
	SQLLEN rowCount;
	/* get a statement handle */
	int rc = SQLAllocHandle(SQL_HANDLE_STMT, conn_res->hdbc, &hstmt);
	check_dbh_error(rc, "SQLAllocHandle");

#if PHP_8_1_OR_HIGHER
	rc = SQLExecDirect(hstmt, (SQLCHAR *) ZSTR_VAL(sql), ZSTR_LEN(sql));
#else
	rc = SQLExecDirect(hstmt, (SQLCHAR *) sql, sql_len);
#endif
	if (rc == SQL_ERROR) {
		/*
		* NB...we raise the error before freeing the handle so that
		* we catch the proper error record.
		*/
		raise_sql_error(dbh, NULL, hstmt, SQL_HANDLE_STMT,
			"SQLExecDirect", __FILE__, __LINE__ );
		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);

		/*
		* Things are a bit overloaded here...we're supposed to return a count
		* of the affected rows, but -1 indicates an error occurred.
		*/
		return -1;
	}

	/*
	* Check if SQL_NO_DATA_FOUND was returned:
	* SQL_NO_DATA_FOUND is returned if the SQL statement is a Searched UPDATE
	* or Searched DELETE and no rows satisfy the search condition.
	*/
	if (rc == SQL_NO_DATA) {
		rowCount = 0;
	} else {
		/* we need to update the number of affected rows. */
		rc = SQLRowCount(hstmt, &rowCount);
		if (rc == SQL_ERROR) {
			/*
			* NB...we raise the error before freeing the handle so that
			* we catch the proper error record.
			*/
			raise_sql_error(dbh, NULL, hstmt, SQL_HANDLE_STMT,
				"SQLRowCount", __FILE__, __LINE__ );
			SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
			return -1;
		}
		/*
		* -1 will be retuned if the following:
		* If the last executed statement referenced by the input statement handle
		* was not an UPDATE, INSERT, DELETE, or MERGE statement, or if it did not
		* execute successfully, then the function sets the contents of RowCountPtr to -1.
		*/
		if (rowCount == -1) {
			rowCount = 0;
		}
	}

	/* Set the last serial id inserted */
	rc = record_last_insert_id(dbh, hstmt );
	if( rc == SQL_ERROR ) {
		return -1;
	}
	/* this is a one-shot deal, so make sure we free the statement handle */
	SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
	return rowCount;
}

/* start a new transaction */
static STATUS_RETURN_TYPE informix_handle_begin( pdo_dbh_t *dbh )
{
	conn_handle *conn_res = (conn_handle *) dbh->driver_data;
	int rc = SQLSetConnectAttr(conn_res->hdbc, SQL_ATTR_AUTOCOMMIT,
			(SQLPOINTER) SQL_AUTOCOMMIT_OFF, SQL_NTS);
	check_dbh_error(rc, "SQLSetConnectAttr");
	return TRUE;
}

static STATUS_RETURN_TYPE informix_handle_commit(
	pdo_dbh_t *dbh
	)
{
	conn_handle *conn_res = (conn_handle *)dbh->driver_data;

	int rc = SQLEndTran(SQL_HANDLE_DBC, conn_res->hdbc, SQL_COMMIT);
	check_dbh_error(rc, "SQLEndTran");
	if (dbh->auto_commit != 0) {
		rc = SQLSetConnectAttr(conn_res->hdbc, SQL_ATTR_AUTOCOMMIT,
				(SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_NTS);
		check_dbh_error(rc, "SQLSetConnectAttr");
	}
	return TRUE;
}

static STATUS_RETURN_TYPE informix_handle_rollback(
	pdo_dbh_t *dbh
	)
{
	conn_handle *conn_res = (conn_handle *)dbh->driver_data;

	int rc = SQLEndTran(SQL_HANDLE_DBC, conn_res->hdbc, SQL_ROLLBACK);
	check_dbh_error(rc, "SQLEndTran");
	if (dbh->auto_commit != 0) {
		rc = SQLSetConnectAttr(conn_res->hdbc, SQL_ATTR_AUTOCOMMIT,
				(SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_NTS);
		check_dbh_error(rc, "SQLSetConnectAttr");
	}
	return TRUE;
}

/* Set the driver attributes. We allow the setting of autocommit */
static STATUS_RETURN_TYPE informix_handle_set_attribute(
	pdo_dbh_t *dbh,
	long attr,
	zval *return_value
	)
{
	conn_handle *conn_res = (conn_handle *)dbh->driver_data;
	int rc = 0;
        int newAutocommit = 0;
 
#if PHP_MAJOR_VERSION >=7
        if (Z_TYPE_P(return_value) == IS_TRUE
              || (Z_TYPE_P(return_value) == IS_LONG && Z_LVAL_P(return_value))) {
            newAutocommit = 1;
        }  
#else
        if (Z_TYPE_P(return_value) == TRUE
              || (Z_TYPE_P(return_value) == IS_LONG && Z_LVAL_P(return_value))) {
            newAutocommit = 1;
        }  
#endif

	switch (attr) {
		case PDO_ATTR_AUTOCOMMIT:
			if (dbh->auto_commit != newAutocommit) {
				dbh->auto_commit = newAutocommit;
				if (dbh->auto_commit != 0) {
					rc = SQLSetConnectAttr((SQLHDBC) conn_res->hdbc, SQL_ATTR_AUTOCOMMIT,
						(SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_NTS);
					check_dbh_error(rc, "SQLSetConnectAttr");
				} else {
					rc = SQLSetConnectAttr((SQLHDBC) conn_res->hdbc, SQL_ATTR_AUTOCOMMIT,
						(SQLPOINTER) SQL_AUTOCOMMIT_OFF, SQL_NTS);
					check_dbh_error(rc, "SQLSetConnectAttr");
				}
			}
			return TRUE;
			break;
		default:
			return FALSE;
	}
}

/* fetch the last inserted serial id */
#if PHP_8_1_OR_HIGHER
static zend_string *informix_handle_lastInsertID(pdo_dbh_t * dbh, const zend_string *name)
#else
static char *informix_handle_lastInsertID(pdo_dbh_t * dbh, const char *name, size_t *len )
#endif
{
	char *id = emalloc(20);
	int rc = 0;
	conn_handle *conn_res = (conn_handle *) dbh->driver_data;
#if PHP_8_1_OR_HIGHER
	zend_string *last_id_zstr;
#endif
	sprintf(id, "%d", conn_res->last_insert_id);
#if PHP_8_1_OR_HIGHER
        last_id_zstr = zend_string_init(id, strlen(id), 0);
        efree(id);
        return last_id_zstr;
#else
	*len = strlen(id);
	return id;
#endif

}

/* fetch the supplemental error material */
static NO_STATUS_RETURN_TYPE informix_handle_fetch_error(
	pdo_dbh_t *dbh,
	pdo_stmt_t *stmt,
	zval *info
	)
{
	conn_handle *conn_res = (conn_handle *)dbh->driver_data;
	char suppliment[512];

	if(conn_res->error_data.failure_name == NULL && conn_res->error_data.filename == NULL) {
		conn_res->error_data.filename="(null)";
		conn_res->error_data.failure_name="(null)";
	}
     if(conn_res->error_data.isam_err != 0) {
	     sprintf(suppliment, "%s (%s[%d] at %s:%d) ISAM: %d", 
	              conn_res->error_data.err_msg,		/*  an associated message */
    		      conn_res->error_data.failure_name,	/*  the routine name */
      		      conn_res->error_data.sqlcode,		/*  native error code of the failure */
		      conn_res->error_data.filename,		/*  source file of the reported error */
		      conn_res->error_data.lineno,		/*  location of the reported error */
                      conn_res->error_data.isam_err); /*  ISAM Error message */
     } else {
	     sprintf(suppliment, "%s (%s[%d] at %s:%d) ISAM: %d", 
	              conn_res->error_data.err_msg,		/*  an associated message */
    		      conn_res->error_data.failure_name,	/*  the routine name */
      		      conn_res->error_data.sqlcode,		/*  native error code of the failure */
		      conn_res->error_data.filename,		/*  source file of the reported error */
		      conn_res->error_data.lineno);		/*  location of the reported error */
     }
      
	/*
	 * Now add the error information.  These need to be added
	 * in a specific order
	 */
	add_next_index_long(info, conn_res->error_data.sqlcode);
#if PHP_MAJOR_VERSION >=7
	add_next_index_string(info, suppliment);
#else
	add_next_index_string(info, suppliment, 1);
#endif

#if !PHP_8_1_OR_HIGHER
	return TRUE;
#endif
}

/* quotes an SQL statement */
#if PHP_8_1_OR_HIGHER
static zend_string* informix_handle_quoter(
	pdo_dbh_t *dbh,
	const zend_string *unquoted,
	enum pdo_param_type paramtype)
#else
static int informix_handle_quoter(
	pdo_dbh_t *dbh,
	const char *unq,
	size_t unq_len,
	char **q,
	size_t *q_len,
	enum pdo_param_type paramtype)
#endif
{
	char *sql;
	int new_length, i, j;
#if PHP_8_1_OR_HIGHER
	/* keep the logic close to the pre-8.1 version w/ compat vars */
        const char *unq = ZSTR_VAL(unquoted);
	size_t unq_len = ZSTR_LEN(unquoted);
	zend_string *quoted;
#endif

#if PHP_8_1_OR_HIGHER
	/* AFAIK we can't get a non-null unquoted */
	if (unq_len == 0) {
		return zend_string_init("''", 2, 0);
	}
#else
	if(!unq)  {
		return FALSE;
	}
#endif

	/* allocate twice the source length first (worst case) */
	sql = (char*)emalloc(((unq_len*2)+3)*sizeof(char));

	/* set the first quote */
	sql[0] = '\'';

	j = 1;
	for (i = 0; i < unq_len; i++) {
		switch (unq[i]) {
			case '\n':
				sql[j++] = '\\';
				sql[j++] = 'n';
				break;
			case '\r':
				sql[j++] = '\\';
				sql[j++] = 'r';
				break;
			case '\x1a':
				sql[j++] = '\\';
				sql[j++] = 'Z';
				break;
			case '\0':
				sql[j++] = '\\';
				sql[j++] = '0';
				break;
			case '\'':
				sql[j++] = '\\';
				sql[j++] = '\'';
				break;
			case '\"':
				sql[j++] = '\\';
				sql[j++] = '\"';
				break;
			case '\\':
				sql[j++] = '\\';
				sql[j++] = '\\';
				break;
			default:
				sql[j++] = unq[i];
				break;
		}
	}

	/* set the last quote and null terminating character */
	sql[j++] = '\'';
	sql[j++] = '\0';

	/* copy over final string and free the memory used */
#if PHP_8_1_OR_HIGHER
	quoted = zend_string_init(sql, strlen(sql), 0);
	efree(sql);
	return quoted;
#else
	*q = (char*)emalloc(((unq_len*2)+3)*sizeof(char));
	strcpy(*q, sql);
	*q_len = strlen(sql);
	efree(sql);

	return TRUE;
#endif
}


/* Get the driver attributes. We return the autocommit and version information. */
static int informix_handle_get_attribute(
	pdo_dbh_t *dbh,
	long attr,
	zval *return_value
	)
{
	char value[MAX_DBMS_IDENTIFIER_NAME];
	int rc;
	conn_handle *conn_res = (conn_handle *) dbh->driver_data;
	SQLINTEGER tc_flag;



	switch (attr) {
		case PDO_ATTR_CLIENT_VERSION:
#if PHP_MAJOR_VERSION >= 7
			ZVAL_STRING(return_value, PDO_INFORMIX_VERSION);
#else
			ZVAL_STRING(return_value, PDO_INFORMIX_VERSION, 1);
#endif
			return TRUE;

		case PDO_ATTR_AUTOCOMMIT:
			ZVAL_BOOL(return_value, dbh->auto_commit);
			return TRUE;

		case PDO_ATTR_SERVER_INFO:
			rc = SQLGetInfo(conn_res->hdbc, SQL_DBMS_NAME, 
					(SQLPOINTER)value, MAX_DBMS_IDENTIFIER_NAME, NULL);
			check_dbh_error(rc, "SQLGetInfo");
#if PHP_MAJOR_VERSION >= 7
			ZVAL_STRING(return_value, value);
#else
			ZVAL_STRING(return_value, value, 1);
#endif
			return TRUE;


	}
	return FALSE;
}


static struct pdo_dbh_methods informix_dbh_methods = {
	informix_handle_closer,
	informix_handle_preparer,
	informix_handle_doer,
	informix_handle_quoter,		
	informix_handle_begin,
	informix_handle_commit,
	informix_handle_rollback,
	informix_handle_set_attribute,
	informix_handle_lastInsertID,
	informix_handle_fetch_error,
	informix_handle_get_attribute,
	NULL,				/* check_liveness  */
	NULL				/* get_driver_methods */
};

/* handle the business of creating a connection. */
static int dbh_connect(pdo_dbh_t *dbh, zval *driver_options )
{
	int rc = 0;
	int dsn_length = 0;
	char *new_dsn = NULL;
	SQLSMALLINT d_length = 0, u_length = 0, p_length = 0;
	/*
	* Allocate our driver data control block.  If this is a persistent
	* connection, we need to allocate this from persistent storage.
	*/
	conn_handle *conn_res = (conn_handle *) pemalloc(sizeof(conn_handle), dbh->is_persistent);
	check_allocation(conn_res, "dbh_connect", "Unable to allocate driver data");

	/* clear, and hook up to the PDO data structure. */
	memset((void *) conn_res, '\0', sizeof(conn_handle));
	dbh->driver_data = conn_res;

	/* we need an environment to use for a base */
	rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &conn_res->henv);
	check_dbh_error(rc, "SQLAllocHandle");
	/* and we're using the OBDC version 3 style interface */
	rc = SQLSetEnvAttr((SQLHENV)conn_res->henv, SQL_ATTR_ODBC_VERSION,
			(void *) SQL_OV_ODBC3, 0);
	check_dbh_error(rc, "SQLSetEnvAttr");

	/* now an actual connection handle */
	rc = SQLAllocHandle(SQL_HANDLE_DBC, conn_res->henv, &(conn_res->hdbc));
	check_dbh_error(rc, "SQLAllocHandle");

		
	/*
	* NB:  We don't have any specific driver options we support at this time, so
	* we don't need to do any option parsing. If the string contains a =, then
	* we need to use SQLDriverConnect to make the connection.  This may require
	* reform  var_dump($rows);atting the DSN string to include a userid and
	* password.
	*/
	if (strchr(dbh->data_source, '=') != NULL) {
		/* first check to see if we have a user name */
		if (dbh->username != NULL && dbh->password != NULL) {
			/*
			* Ok, one was given...however, the DSN may already contain UID
			* information, so check first.
			*/
			if (strstr(dbh->data_source, ";uid=") == NULL
					&& strstr(dbh->data_source, ";UID=") == NULL) {
				/* Make sure each of the connection parameters is not NULL */
				d_length = strlen(dbh->data_source);
				u_length = strlen(dbh->username);
				p_length = strlen(dbh->password);
				dsn_length = d_length + u_length + p_length + sizeof(";UID=;PWD=;") + 1;
				new_dsn = pemalloc(dsn_length, dbh->is_persistent);
				check_allocation(new_dsn, "dbh_connect", "unable to allocate DSN string");
				sprintf(new_dsn, "%s;UID=%s;PWD=%s;", dbh->data_source,
						dbh->username, dbh->password);
				if (dbh->data_source) {
					pefree((void *) dbh->data_source, dbh->is_persistent);
				}
				/* now replace the DSN with a properly formatted one. */
				dbh->data_source = new_dsn;
			}

		}
		/* and finally try to connect */
		rc = SQLDriverConnect((SQLHDBC) conn_res->hdbc, (SQLHWND) NULL,
				(SQLCHAR *) dbh->data_source, SQL_NTS, NULL,
				0, NULL, SQL_DRIVER_NOPROMPT);
		check_dbh_error(rc, "SQLDriverConnect");
	} else 
	{
		/* Make sure each of the connection parameters is not NULL */
		if (dbh->data_source) {
			d_length = strlen(dbh->data_source);
		}
		if (dbh->username) {
			u_length = strlen(dbh->username);
		}
		if (dbh->password) {
			p_length = strlen(dbh->password);
		}
		/*
		* No connection options specified, we can just connect with the name,
		*  userid, and password as given.
		*/
		rc = SQLConnect((SQLHDBC) conn_res->hdbc, (SQLCHAR *) dbh->data_source,
			(SQLSMALLINT) d_length,
			(SQLCHAR *) dbh->username,
			(SQLSMALLINT) u_length,
			(SQLCHAR *)dbh->password,
			(SQLSMALLINT) p_length);
		check_dbh_error(rc, "SQLConnect");
	}

	/*
	 * Set NeedODBCTypesOnly=1 because we dont support
	 * Smart Large Objects in PDO yet
	 */
	rc = SQLSetConnectAttr((SQLHDBC) conn_res->hdbc, SQL_INFX_ATTR_LO_AUTOMATIC,
			(SQLPOINTER) SQL_TRUE, SQL_NTS);
	check_dbh_error(rc, "SQLSetConnectAttr");
	rc = SQLSetConnectAttr((SQLHDBC)conn_res->hdbc, SQL_INFX_ATTR_ODBC_TYPES_ONLY,
			(SQLPOINTER) SQL_TRUE, SQL_NTS);
	check_dbh_error(rc, "SQLSetConnectAttr");

	/* if we're in auto commit mode, set the connection attribute. */
	if (dbh->auto_commit != 0) {
		rc = SQLSetConnectAttr((SQLHDBC) conn_res->hdbc, SQL_ATTR_AUTOCOMMIT,
				(SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_NTS);
		check_dbh_error(rc, "SQLSetConnectAttr");
	} else {
		rc = SQLSetConnectAttr((SQLHDBC) conn_res->hdbc, SQL_ATTR_AUTOCOMMIT,
				(SQLPOINTER) SQL_AUTOCOMMIT_OFF, SQL_NTS);
		check_dbh_error(rc, "SQLSetConnectAttr");
	}

	/* set the desired case to be upper */
	dbh->desired_case = PDO_CASE_UPPER;

	/* this is now live!  all error handling goes through normal mechanisms. */
	dbh->methods = &informix_dbh_methods;
	dbh->alloc_own_columns = 1;
	return TRUE;
}


/*
* Main routine called to create a connection.  The dbh structure is
* allocated for us, and we attached a driver-specific control block
* to the PDO allocated one,
*/
static int informix_handle_factory(
	pdo_dbh_t *dbh,
	zval *driver_options
	)
{
	/* go do the connection */
	return dbh_connect(dbh, driver_options );
}

pdo_driver_t pdo_informix_driver =
{
	PDO_DRIVER_HEADER(informix),
	informix_handle_factory
};

/* common error handling path for final disposition of an error.*/
static void process_pdo_error(pdo_dbh_t *dbh, pdo_stmt_t *stmt )
{
/*  current_error_state(dbh);*/

	conn_handle *conn_res = (conn_handle *)dbh->driver_data;
	strcpy(dbh->error_code, conn_res->error_data.sql_state);
	if (stmt != NULL) {
		/* what this a error in the stmt constructor? */
		if (stmt->methods == NULL) {
			/* make sure we do any required cleanup. */
			informix_stmt_dtor(stmt );
		}
		strcpy(stmt->error_code, conn_res->error_data.sql_state);
	}

	/*
	* if we got an error very early, we need to throw an exception rather than
	* use the PDO error reporting.
	*/

	if (dbh->methods == NULL) {
            if(conn_res->error_data.isam_err == 0) {
		zend_throw_exception_ex(php_pdo_get_exception(), 0 ,
				"SQLSTATE=%s, %s: %d %s",
				conn_res->error_data.sql_state,
				conn_res->error_data.failure_name,
				conn_res->error_data.sqlcode,
				conn_res->error_data.err_msg);
            } else {
		zend_throw_exception_ex(php_pdo_get_exception(), 0 ,
				"SQLSTATE=%s, %s: %d %s",
				conn_res->error_data.sql_state,
				conn_res->error_data.failure_name,
				conn_res->error_data.sqlcode,
				conn_res->error_data.err_msg,
                                conn_res->error_data.isam_err);
           }
  	   informix_handle_closer(dbh );
	}
}

/*
* Handle an error return from an SQL call.  The error information from the
* call is saved in our error record.
*/
void raise_sql_error(pdo_dbh_t *dbh, pdo_stmt_t *stmt, SQLHANDLE handle,
	SQLSMALLINT hType, char *tag, char *file, int line )
{
        int rc;
	SQLSMALLINT length;
	conn_handle *conn_res = (conn_handle *) dbh->driver_data;

	conn_res->error_data.failure_name = tag;
	conn_res->error_data.filename = file;
	conn_res->error_data.lineno = line;

	SQLGetDiagRec(hType, handle, 1, (SQLCHAR *) & (conn_res->error_data.sql_state),
		&(conn_res->error_data.sqlcode),
		(SQLCHAR *) & (conn_res->error_data.err_msg),
		SQL_MAX_MESSAGE_LENGTH, &length);
	/* the error message is not returned null terminated. */
	conn_res->error_data.err_msg[length] = '\0';

        if(hType == SQL_HANDLE_STMT) {
             /* This is the actual call that returns the ISAM error */
             rc = SQLGetDiagField(SQL_HANDLE_STMT, handle, 1, SQL_DIAG_ISAM_ERROR,
                        (SQLPOINTER) & conn_res->error_data.isam_err, SQL_IS_INTEGER, NULL);
        }     
	
        /* now go tell PDO about this problem */
	process_pdo_error(dbh, stmt );
}

/*
* Raise a driver-detected error.  This is a faked-SQL type error, using a
* provided sqlstate and message info.
*/
void raise_informix_error(pdo_dbh_t *dbh, pdo_stmt_t *stmt, char *state, char *tag,
	char *message, char *file, int line )
{
	conn_handle *conn_res = (conn_handle *) dbh->driver_data;

	conn_res->error_data.failure_name = tag;
	conn_res->error_data.filename = file;
	conn_res->error_data.lineno = line;
	strcpy(conn_res->error_data.err_msg, message);

	strcpy(conn_res->error_data.sql_state, state);
	conn_res->error_data.sqlcode = 1;	/* just give a non-zero code state. */
	/* now go tell PDO about this problem */
	process_pdo_error(dbh, stmt );
}

/*
* Raise an error in a connection context.  This ensures we use the
* connection handle for retrieving error information.
*/
void raise_dbh_error(pdo_dbh_t *dbh, char *tag, char *file, int line )
{
	conn_handle *conn_res = (conn_handle *) dbh->driver_data;
	raise_sql_error(dbh, NULL, conn_res->hdbc, SQL_HANDLE_DBC, tag, file,
			line );
}

/*
* Raise an error in a statement context.  This ensures we use the correct
* handle for retrieving the diag record, as well as forcing stmt-related
* cleanup.
*/
void raise_stmt_error(pdo_stmt_t *stmt, char *tag, char *file, int line )
{
	stmt_handle *stmt_res = (stmt_handle *) stmt->driver_data;

	/* if we're in the middle of execution when an error was detected, make sure we cancel */
	if (stmt_res->executing) {
		/*  raise the error */
		raise_sql_error(stmt->dbh, stmt, stmt_res->hstmt, SQL_HANDLE_STMT, tag, file, line );
		/*  cancel the statement */
		SQLCancel(stmt_res->hstmt);
		/*  make sure we release execution-related storage. */
		if (stmt_res->lob_buffer != NULL) {
			efree(stmt_res->lob_buffer);
			stmt_res->lob_buffer = NULL;
		}
		if (stmt_res->converted_statement != NULL) {
			efree(stmt_res->converted_statement);
			stmt_res->converted_statement = NULL;
		}
		stmt_res->executing = 0;
	} else {
		/*  raise the error */
		raise_sql_error(stmt->dbh, stmt, stmt_res->hstmt, SQL_HANDLE_STMT, tag, file, line );
	}
}

/*
* Clears the error information
*/
void clear_stmt_error(pdo_stmt_t *stmt)
{
	conn_handle *conn_res = (conn_handle *) stmt->dbh->driver_data;

	conn_res->error_data.sqlcode			= 0;
	conn_res->error_data.filename			= NULL;
	conn_res->error_data.lineno				= 0;
	conn_res->error_data.failure_name		= NULL;
	conn_res->error_data.sql_state[0]		= '\0';
	conn_res->error_data.err_msg[0]			= '\0';
        conn_res->error_data.isam_err                   = 0;
}
