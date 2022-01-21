/*
  +----------------------------------------------------------------------+
  | (C) Copyright IBM Corporation 2006                                   |
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

#ifndef PHP_PDO_INFORMIX_INT_H
#define PHP_PDO_INFORMIX_INT_H

#include "infxcli.h"

#if PHP_MAJOR_VERSION > 7 || (PHP_MAJOR_VERSION == 7 && PHP_MINOR_VERSION >= 4)
#define PHP_7_4_OR_HIGHER 1
#else
#define PHP_7_4_OR_HIGHER 0
#endif
#if PHP_MAJOR_VERSION > 8 || (PHP_MAJOR_VERSION == 8 && PHP_MINOR_VERSION >= 1)
#define PHP_8_1_OR_HIGHER 1
#else
#define PHP_8_1_OR_HIGHER 0
#endif

#define MAX_OPTION_LEN 10
#define MAX_ERR_MSG_LEN (SQL_MAX_MESSAGE_LENGTH + SQL_SQLSTATE_SIZE + 1)
#define CDTIMETYPE 112

#ifndef SQL_XML
#define SQL_XML -370
#endif

/* Maximum length of the name of the DBMS being accessed */
#define MAX_DBMS_IDENTIFIER_NAME 256


#ifndef SQL_ATTR_GET_GENERATED_VALUE
#define SQL_ATTR_GET_GENERATED_VALUE 2583
#endif

#define MAX_ISAM_ERROR_MSG_LEN MAX_ERR_MSG_LEN

/* This function is called after executing a stmt for recording lastInsertId */
int record_last_insert_id( pdo_dbh_t *dbh, SQLHANDLE hstmt );


/* error handling functions and macros. */
void raise_sql_error(pdo_dbh_t *dbh, pdo_stmt_t *stmt, SQLHANDLE handle, SQLSMALLINT hType, char *tag, char *file, int line );
void raise_informix_error(pdo_dbh_t *dbh, pdo_stmt_t *stmt, char *state, char *tag, char *message, char *file, int line );
void raise_dbh_error(pdo_dbh_t *dbh, char *tag, char *file, int line );
void raise_stmt_error(pdo_stmt_t *stmt, char *tag, char *file, int line );
void clear_stmt_error(pdo_stmt_t *stmt);
int informix_stmt_dtor(pdo_stmt_t *stmt );

#define RAISE_DBH_ERROR(tag) raise_dbh_error(dbh, tag, __FILE__, __LINE__ )
#define RAISE_STMT_ERROR(tag) raise_stmt_error(stmt, tag, __FILE__, __LINE__ )
#define RAISE_INFORMIX_STMT_ERROR(state, tag, msg) raise_informix_error(stmt->dbh, stmt, state, tag, msg, __FILE__, __LINE__ )
#define RAISE_INFORMIX_DBH_ERROR(state, tag, msg) raise_informix_error(dbh, NULL, state, tag, msg, __FILE__, __LINE__ )

/* check for an SQL error in the context of an 
   PDO method execution. */
#define check_dbh_error(rc, tag)	\
{									\
	if ( rc == SQL_ERROR )			\
	{								\
		RAISE_DBH_ERROR(tag);		\
		return FALSE;				\
	}								\
}									\

/* check for an SQL error in the context of an 
   PDOStatement method execution. */
#define check_stmt_error(rc, tag)	\
				{					\
	if ( rc == SQL_ERROR )			\
	{								\
		RAISE_STMT_ERROR(tag);		\
		return FALSE;				\
	}								\
}									\

/* check an allocation in the context of a PDO object. */
#define check_allocation(ptr, tag, msg)					\
{														\
	if ((ptr) == NULL)									\
	{													\
		RAISE_INFORMIX_DBH_ERROR("HY001", tag, msg);	\
		return FALSE;									\
	}													\
}														\

/* check a storage allocation for a PDOStatement
   object context. */
#define check_stmt_allocation(ptr, tag, msg)				\
{															\
	if ((ptr) == NULL)										\
	{														\
		RAISE_INFORMIX_STMT_ERROR("HY001", tag, msg);	\
		return FALSE;										\
	}														\
}															\


typedef struct _conn_error_data {
	SQLINTEGER sqlcode;							/* native sql error code */
	char *filename;								/* name of the file raising the error */
	int lineno;									/* line number location of the error */
	char *failure_name;							/* the failure tag. */
	SQLCHAR sql_state[8];						/* SQLSTATE code */
	char err_msg[SQL_MAX_MESSAGE_LENGTH + 1];	/* error message associated with failure */
        SQLINTEGER isam_err;
} conn_error_data;

typedef struct _conn_handle_struct {
	SQLHANDLE henv;				/* handle to the interface environment */
	SQLHANDLE hdbc;				/* the connection handle */
	conn_error_data error_data;	/* error handling information */
	int last_insert_id;			/* the last serial id inserted */
} conn_handle;

/* values used for binding fetched data */
typedef union {
	long l_val;		/* long values -- used for all int values, including bools */
	char *str_val;	/* used for string bindings */
} column_data_value;

/* local descriptor for column data.  These mirror the
   descriptors given back to the PDO driver. */
typedef struct {
#if PHP_MAJOR_VERSION >= 7
        zend_string *name;
#else
	char *name;							/* the column name */
#endif
	SQLSMALLINT namelen;				/* length of the column name */
	SQLSMALLINT data_type;				/* the database column type */
	enum pdo_param_type returned_type;	/* our returned parameter type */
	SQLULEN data_size;				/* maximum size of the data  */
	SQLSMALLINT nullable;				/* the nullable flag */
	SQLSMALLINT scale;					/* the scale value */
	SQLULEN out_length;				/* the transfered data length. Filled in by a fetch */
	column_data_value data;				/* the transferred data */
} column_data;

/* size of the buffer used to read LOB streams */
#define LOB_BUFFER_SIZE 8192

/*
 * PHP 8.1 changes many functions to be bool instead of int
 */
#if PHP_8_1_OR_HIGHER
#define STATUS_RETURN_TYPE bool
#define NO_STATUS_RETURN_TYPE void
#else
#define STATUS_RETURN_TYPE int
#define NO_STATUS_RETURN_TYPE int
#endif

typedef struct _stmt_handle_struct {
	SQLHANDLE hstmt;					/* the statement handle associated with the stmt */
	int executing;						/* an executing state flag for error cleanup */
#if PHP_8_1_OR_HIGHER
	zend_string *converted_statement;			/* temporary version of the statement with parameter replacement */
#else
	char *converted_statement;			/* temporary version of the statement with parameter replacement */
#endif
	char *lob_buffer;					/* buffer used for reading in LOB parameters */
	column_data *columns;				/* the column descriptors */
	enum pdo_cursor_type cursor_type;	/* the type of cursor we support. */
	SQLSMALLINT server_ver;				/* the server version */
} stmt_handle;

/* Defines the driver_data structure for caching param data */
typedef struct _param_node {
	SQLSMALLINT	data_type;			/* The database data type */
	SQLULEN	param_size;			/* param size */
	SQLSMALLINT nullable;			/* is Nullable  */
	SQLSMALLINT	scale;				/* Decimal scale */
	SQLSMALLINT ctype;				/* the optimal C type for transfer */
	SQLLEN  transfer_length;	/* the transfer length of the parameter */
} param_node;

#endif
