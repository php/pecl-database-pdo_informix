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
#include "pdo/php_pdo_driver.h"
#include "php_pdo_informix.h"
#include "php_pdo_informix_int.h"

/* If you declare any globals in php_pdo_informix.h uncomment this:
ZEND_DECLARE_MODULE_GLOBALS(pdo_informix)
*/

/* True global resources - no need for thread safety here */
static int le_pdo_informix;
extern pdo_driver_t pdo_informix_driver;	/* the registration table */

/* {{{ pdo_informix_functions[]
 *
 * Every user visible function must have an entry in pdo_informix_functions[].
 */
zend_function_entry pdo_informix_functions[] =
{
	PHP_FE(confirm_pdo_informix_compiled, NULL)	/* For testing, remove later. */
	{
		NULL, NULL, NULL
	}	/* Must be the last line in pdo_informix_functions[] */
};
/* }}} */

/* {{{ pdo_informix_deps
 */
#if ZEND_MODULE_API_NO >= 20041225
static zend_module_dep pdo_informix_deps[] = {
	ZEND_MOD_REQUIRED("pdo")
	{NULL, NULL, NULL}
};
#endif
/* }}} */

/* {{{ pdo_informix_module_entry
 */
zend_module_entry pdo_informix_module_entry =
{
#if ZEND_MODULE_API_NO >= 20041225
	STANDARD_MODULE_HEADER_EX, NULL,
	pdo_informix_deps,
#else
	STANDARD_MODULE_HEADER,
#endif
	"pdo_informix",
	pdo_informix_functions,
	PHP_MINIT(pdo_informix),
	PHP_MSHUTDOWN(pdo_informix),
	PHP_RINIT(pdo_informix),        /* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(pdo_informix),    /* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(pdo_informix),
	PDO_INFORMIX_VERSION,   /* Replace with version number for your extension */
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_PDO_INFORMIX
ZEND_GET_MODULE(pdo_informix)
#endif

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("pdo_informix.global_value",      "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_pdo_informix_globals, pdo_informix_globals)
	STD_PHP_INI_ENTRY("pdo_informix.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_pdo_informix_globals, pdo_informix_globals)
PHP_INI_END()
*/
/* }}} */

/* {{{ php_pdo_informix_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_pdo_informix_init_globals(zend_pdo_informix_globals *pdo_informix_globals)
{
	pdo_informix_globals->global_value = 0;
	pdo_informix_globals->global_string = NULL;
}
*/
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(pdo_informix)
{
	/* If you have INI entries, uncomment these lines
	ZEND_INIT_MODULE_GLOBALS(pdo_informix, php_pdo_informix_init_globals, NULL);
	REGISTER_INI_ENTRIES();
	*/

	
	php_pdo_register_driver(&pdo_informix_driver);
	return TRUE;  
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(pdo_informix)
{
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/
	 
	php_pdo_unregister_driver(&pdo_informix_driver);
	return TRUE;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(pdo_informix)
{
	return TRUE;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(pdo_informix)
{
	return TRUE;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(pdo_informix)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "pdo_informix support", "enabled");
	php_info_print_table_row(2, "Module release", PDO_INFORMIX_VERSION);
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */


/* Remove the following function when you have succesfully modified config.m4
   so that your module can be compiled into PHP, it exists only for testing
   purposes. */

/* Every user-visible function in PHP should document itself in the source */
/* {{{ proto string confirm_pdo_informix_compiled(string arg)
   Return a string to confirm that the module is compiled in */
PHP_FUNCTION(confirm_pdo_informix_compiled)
{
	char *arg = NULL;
	int arg_len, len;
	char string[256];

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &arg, &arg_len) == FALSE)
	{
		return;
	}

	len = sprintf(string, "Congratulations! You have successfully modified ext/%.78s/config.m4. Module %.78s is now compiled into PHP.", "pdo_informix", arg);
	RETURN_STRINGL(string, len, 1);
}
/* }}} */
