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

#ifndef PHP_PDO_INFORMIX_H
#define PHP_PDO_INFORMIX_H

#define PDO_INFORMIX_VERSION "1.3.1"

extern zend_module_entry pdo_informix_module_entry;
#define phpext_pdo_informix_ptr &pdo_informix_module_entry

#ifdef PHP_WIN32
#define PHP_PDO_INFORMIX_API __declspec(dllexport)
#else
#define PHP_PDO_INFORMIX_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

PHP_MINIT_FUNCTION(pdo_informix);
PHP_MSHUTDOWN_FUNCTION(pdo_informix);
PHP_RINIT_FUNCTION(pdo_informix);
PHP_RSHUTDOWN_FUNCTION(pdo_informix);
PHP_MINFO_FUNCTION(pdo_informix);

PHP_FUNCTION(confirm_pdo_informix_compiled);	/* For testing, remove later. */

/* 
	Declare any global variables you may need between the BEGIN
	and END macros here:     

ZEND_BEGIN_MODULE_GLOBALS(pdo_informix)
	long  global_value;
	char *global_string;
ZEND_END_MODULE_GLOBALS(pdo_informix)
*/

/*
	In every utility function you add that needs to use variables 
	in php_pdo_informix_globals, call TSRMLS_FETCH(); after declaring other 
	variables used by that function, or better yet, pass in TSRMLS_CC
	after the last function argument and declare your utility function
	with TSRMLS_DC after the last declared argument.  Always refer to
	the globals in your function as PDO_INFORMIX_G(variable).  You are 
	encouraged to rename these macros something shorter, see
	examples in any other php module directory.
*/

#ifdef ZTS
#define PDO_INFORMIX_G(v) TSRMG(pdo_informix_globals_id, zend_pdo_informix_globals *, v)
#else
#define PDO_INFORMIX_G(v) (pdo_informix_globals.v)
#endif

#endif	/* PHP_PDO_INFORMIX_H */
