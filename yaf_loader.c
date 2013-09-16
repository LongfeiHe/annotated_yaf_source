/*
  +----------------------------------------------------------------------+
  | Yet Another Framework                                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Xinchen Hui  <laruence@php.net>                              |
  +----------------------------------------------------------------------+
*/

/* $Id: yaf_loader.c 328824 2012-12-18 10:13:17Z remi $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "main/SAPI.h"
#include "Zend/zend_alloc.h"
#include "ext/standard/php_smart_str.h"
#include "TSRM/tsrm_virtual_cwd.h"

#include "php_yaf.h"
#include "yaf_application.h"
#include "yaf_namespace.h"
#include "yaf_request.h"
#include "yaf_loader.h"
#include "yaf_exception.h"

zend_class_entry *yaf_loader_ce;

/** {{{ ARG_INFO
 */
ZEND_BEGIN_ARG_INFO_EX(yaf_loader_void_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(yaf_loader_getinstance_arginfo, 0, 0, 0)
    ZEND_ARG_INFO(0, local_library_path)
    ZEND_ARG_INFO(0, global_library_path)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(yaf_loader_autoloader_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, class_name)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(yaf_loader_regnamespace_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, name_prefix)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(yaf_loader_islocalname_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, class_name)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(yaf_loader_import_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, file)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(yaf_loader_setlib_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, library_path)
    ZEND_ARG_INFO(0, is_global)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(yaf_loader_getlib_arginfo, 0, 0, 0)
    ZEND_ARG_INFO(0, is_global)
ZEND_END_ARG_INFO()
/* }}} */

/** {{{ int yaf_loader_register(TSRMLS_D)
 *	利用spl_autoload_register调用类自身的成员方法autoload进行注册 
 *	spl_autoload_register(array($this, 'autoload'));
 */
int yaf_loader_register(yaf_loader_t *loader TSRMLS_DC) {
	zval *autoload, *method, *function, *ret = NULL;
	zval **params[1] = {&autoload};

	/* $autoload = array() */
	MAKE_STD_ZVAL(autoload);
	array_init(autoload);
	/* $method = 'autoload' */
	MAKE_STD_ZVAL(method);
	ZVAL_STRING(method, YAF_AUTOLOAD_FUNC_NAME, 1);
	/* $autoload[] = $loader */
	zend_hash_next_index_insert(Z_ARRVAL_P(autoload), &loader, sizeof(yaf_loader_t *), NULL);
	/* $autoload[] = $method */
	zend_hash_next_index_insert(Z_ARRVAL_P(autoload), &method, sizeof(zval *), NULL);
	/* $function = 'spl_autoload_register' */
	MAKE_STD_ZVAL(function);
	ZVAL_STRING(function, YAF_SPL_AUTOLOAD_REGISTER_NAME, 0);

	/**
	 *	spl_autoload_register(array($this, 'autoload'));
	 */
	do {
		zend_fcall_info fci = {
			sizeof(fci),
			EG(function_table),
			function,
			NULL,
			&ret,
			1,
			(zval ***)params,
			NULL,
			1
		};

		if (zend_call_function(&fci, NULL TSRMLS_CC) == FAILURE) {
			if (ret) {
				zval_ptr_dtor(&ret);
			}
			efree(function);
			zval_ptr_dtor(&autoload);
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to register autoload function %s", YAF_AUTOLOAD_FUNC_NAME);
			return 0;
		}

		/*{{{ no use anymore
		if (0 && !YAF_G(use_spl_autoload)) {
			zend_function *reg_function;
			zend_internal_function override_function = {
				ZEND_INTERNAL_FUNCTION,
				YAF_AUTOLOAD_FUNC_NAME,
				NULL,
				ZEND_ACC_PUBLIC,
				NULL,
				1,
				0,
				NULL,
				0,
				0,
				ZEND_FN(yaf_override_spl_autoload),
				NULL
			};
			zend_internal_function *internal_function = (zend_internal_function *)&override_function;
			internal_function->type 	= ZEND_INTERNAL_FUNCTION;
			internal_function->module 	= NULL;
			internal_function->handler 	= ZEND_FN(yaf_override_spl_autoload);
			internal_function->function_name = YAF_AUTOLOAD_FUNC_NAME;
			internal_function->scope 	=  NULL;
			internal_function->prototype = NULL;
			internal_function->arg_info  = NULL;
			internal_function->num_args  = 1;
			internal_function->required_num_args = 0;
			internal_function->pass_rest_by_reference = 0;
			internal_function->return_reference = 0;
			internal_function->fn_flags = ZEND_ACC_PUBLIC;
			function_add_ref((zend_function*)&override_function);
			//zend_register_functions
			if (zend_hash_update(EG(function_table), YAF_SPL_AUTOLOAD_REGISTER_NAME,
						sizeof(YAF_SPL_AUTOLOAD_REGISTER_NAME), &override_function, sizeof(zend_function), (void **)&reg_function) == FAILURE) {
				YAF_DEBUG("register autoload failed");
				 //no big deal
			}
		}
		}}} */

		if (ret) {
			zval_ptr_dtor(&ret);
		}
		efree(function);
		zval_ptr_dtor(&autoload);
	} while (0);
	return 1;
}
/* }}} */

/** {{{ static int yaf_loader_is_category(char *class, uint class_len, char *category, uint category_len TSRMLS_DC)
 *	这里是根据类名以及所属的目录名，以及yaf配置的前缀或者后缀形式，以及分隔符检查类名是否正确
 *	eg:CategoryModel或者Model_Category形式
 */
static int yaf_loader_is_category(char *class, uint class_len, char *category, uint category_len TSRMLS_DC) {
	/* 分隔符长度 */
	uint separator_len = YAF_G(name_separator_len);
	/* 判断类名前后缀 */
	if (YAF_G(name_suffix)) {
		/* 后缀形式 */
		if (class_len > category_len && strncmp(class + class_len - category_len, category, category_len) == 0) {
			/* 没有分隔符或者分隔符所在位置正确就返回1 */
			if (!separator_len || strncmp(class + class_len - category_len - separator_len, YAF_G(name_separator), separator_len) == 0) {
				return 1;
			}
		}
	} else {
		/* 前缀形式 */
		if (strncmp(class, category, category_len) == 0) {
			/* 没有分隔符或者分隔符所在位置正确就返回1 */
			if (!separator_len || strncmp(class + category_len, YAF_G(name_separator), separator_len) == 0) {
				return 1;
			}
		}
	}

	return 0;
}
/* }}} */

/** {{{ int yaf_loader_is_local_namespace(yaf_loader_t *loader, char *class_name, int len TSRMLS_DC)
 *	判断class_name是否为本地类，穿进去的如果是类名（字符串中带有_或者\\就进行分割，得到前缀），如果不能进行分割则默认认为穿进去的就是前缀
 */
int yaf_loader_is_local_namespace(yaf_loader_t *loader, char *class_name, int len TSRMLS_DC) {
	char *pos, *ns, *prefix = NULL;
	char orig_char = 0, *backup = NULL;
	uint prefix_len = 0;
	/* 判断本地类前缀是否设置，没有的话返回0 */
	if (!YAF_G(local_namespaces)) {
		return 0;
	}

	ns	= YAF_G(local_namespaces);
	/* 在类名里面查找下划线 */
	pos = strstr(class_name, "_");
    if (pos) {
    	/* 前缀长度 */
		prefix_len 	= pos - class_name;
		/* 将类名设置为前缀 */
		prefix 		= class_name;
		/* 类名加前缀长度后的字符串应该是真实的类的名称 */
		backup = class_name + prefix_len;
		orig_char = '_';
		/* 在backup当前位置加上\0，让它成为一个真正的字符串 */
		*backup = '\0';
	}
#ifdef YAF_HAVE_NAMESPACE
	/* 设置了命名空间的话，则使用\\进行查找 */
	else if ((pos = strstr(class_name, "\\"))) {
		/* 前缀长度 */
		prefix_len 	= pos - class_name;
		/* 前缀对类名进行长度截取获得前缀 */
		prefix 		= estrndup(class_name, prefix_len);
		orig_char = '\\';
		/* 类名加前缀长度后的字符串应该是真实的类的名称 */
		backup = class_name + prefix_len;
		/* 在backup当前位置加上\0，让它成为一个真正的字符串 */
		*backup = '\0';
	}
#endif
	else {
		/* 不符合上面的两个的话则认为传进来的直接就是前缀，所以不进行任何截取工作 */
		prefix = class_name;
		prefix_len = len;
	}

	if (!prefix) {
		return 0;
	}
	/** 
	 *	在YAF_G(local_namespaces)中进行prefix字符串的查找
	 *	eg:	:Foo:Bar:
	 */
	while ((pos = strstr(ns, prefix))) {
		if ((pos == ns) && (*(pos + prefix_len) == DEFAULT_DIR_SEPARATOR || *(pos + prefix_len) == '\0')) {
			if (backup) {
				*backup = orig_char;
			}
			return 1;
		} else if (*(pos - 1) == DEFAULT_DIR_SEPARATOR 
				&& (*(pos + prefix_len) == DEFAULT_DIR_SEPARATOR || *(pos + prefix_len) == '\0')) {
			if (backup) {
				*backup = orig_char;
			}
			return 1;
		}
		ns = pos + prefix_len;
	}

	if (backup) {
		*backup = orig_char;
	}

	return 0;
}
/* }}} */

/** {{{ yaf_loader_t * yaf_loader_instance(yaf_loader_t *this_ptr, char *library_path, char *global_path TSRMLS_DC)
 */
yaf_loader_t * yaf_loader_instance(yaf_loader_t *this_ptr, char *library_path, char *global_path TSRMLS_DC) {
	yaf_loader_t *instance;
	zval *glibrary, *library;
	/* 获取Yaf_Loader::$_instance */
	instance = zend_read_static_property(yaf_loader_ce, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_INSTANCE), 1 TSRMLS_CC);

	if (IS_OBJECT == Z_TYPE_P(instance)) {
		/* 类已经实例化 */
	/* unecessary since there is no set_router things
	   && instanceof_function(Z_OBJCE_P(instance), yaf_loader_ce TSRMLS_CC)) {
	 */
		if (library_path) {
			/* 传了本地(自身)类加载路径,则$this->_library = $library */
			MAKE_STD_ZVAL(library);
			ZVAL_STRING(library, library_path, 1);
			zend_update_property(yaf_loader_ce, instance, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_LIBRARY), library TSRMLS_CC);
			zval_ptr_dtor(&library);
		}

		if (global_path) {
			/* 传了全局类加载路径,则$this->_global_library = $glibrary */
			MAKE_STD_ZVAL(glibrary);
			ZVAL_STRING(glibrary, global_path, 1);
			zend_update_property(yaf_loader_ce, instance, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_GLOBAL_LIB), glibrary TSRMLS_CC);
			zval_ptr_dtor(&glibrary);
		}
		/* return Yaf_Loader::$_instance */
		return instance;
	}
	/* 本身类没实例化，并且也没有传入global_path和library_path的话，直接返回NULL */
	if (!global_path && !library_path) {
		return NULL;
	}
	/* 传入了类的实例化则直接使用，没有的话自己实例化 */
	if (this_ptr) {
		instance = this_ptr;
	} else {
		MAKE_STD_ZVAL(instance);
		object_init_ex(instance, yaf_loader_ce);
	}

	/** 
	 *	1.传入两个则分别赋值到类的相应的成员变量
	 *	2.只传入本地类加载路劲则将本地和全局的都赋值为这个值
	 *	3.只传入全局类加载路劲则将本地和全局的都赋值为这个值
	 */
	if (library_path && global_path) {
		MAKE_STD_ZVAL(glibrary);
		MAKE_STD_ZVAL(library);
		ZVAL_STRING(glibrary, global_path, 1);
		ZVAL_STRING(library, library_path, 1);
		zend_update_property(yaf_loader_ce, instance, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_LIBRARY), library TSRMLS_CC);
		zend_update_property(yaf_loader_ce, instance, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_GLOBAL_LIB), glibrary TSRMLS_CC);
		zval_ptr_dtor(&library);
		zval_ptr_dtor(&glibrary);
	} else if (!global_path) {
		MAKE_STD_ZVAL(library);
		ZVAL_STRING(library, library_path, 1);
		zend_update_property(yaf_loader_ce, instance, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_LIBRARY), library TSRMLS_CC);
		zend_update_property(yaf_loader_ce, instance, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_GLOBAL_LIB), library TSRMLS_CC);
		zval_ptr_dtor(&library);
	} else {
		MAKE_STD_ZVAL(glibrary);
		ZVAL_STRING(glibrary, global_path, 1);
		zend_update_property(yaf_loader_ce, instance, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_LIBRARY), glibrary TSRMLS_CC);
		zend_update_property(yaf_loader_ce, instance, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_GLOBAL_LIB), glibrary TSRMLS_CC);
		zval_ptr_dtor(&glibrary);
	}
	/* 注册类自身的成员函数autoload为__autoload()函数 */
	if (!yaf_loader_register(instance TSRMLS_CC)) {
		return NULL;
	}
	/* Yaf_Loader::$_instance = $instance */
	zend_update_static_property(yaf_loader_ce, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_INSTANCE), instance TSRMLS_CC);
	/* return Yaf_Loader::$_instance */
	return instance;
}
/* }}} */

/** {{{ int yaf_loader_import(char *path, int len, int use_path TSRMLS_DC)
 *	整个功能就是对文件的判断，包含，解析以及执行过程
*/
int yaf_loader_import(char *path, int len, int use_path TSRMLS_DC) {
	zend_file_handle file_handle;
	zend_op_array 	*op_array;
	char realpath[MAXPATHLEN];

	if (!VCWD_REALPATH(path, realpath)) {
		return 0;
	}
	/* 组装文件信息结构体 */
	file_handle.filename = path;
	file_handle.free_filename = 0;
	file_handle.type = ZEND_HANDLE_FILENAME;
	file_handle.opened_path = NULL;
	file_handle.handle.fp = NULL;
	/* 解析文件获得字节码 */
	op_array = zend_compile_file(&file_handle, ZEND_INCLUDE TSRMLS_CC);
	/* 解析成功 */
	if (op_array && file_handle.handle.stream.handle) {
		int dummy = 1;
		/* 设置打开文件的绝对路劲 */
		if (!file_handle.opened_path) {
			file_handle.opened_path = path;
		}
		/* 往EG(included_files)添加以文件绝对路劲为key，1为value的键值对，标志文件已经被包含进来 */
		zend_hash_add(&EG(included_files), file_handle.opened_path, strlen(file_handle.opened_path)+1, (void *)&dummy, sizeof(int), NULL);
	}
	/* 销毁打开的文件句柄 */
	zend_destroy_file_handle(&file_handle TSRMLS_CC);

	if (op_array) {
		zval *result = NULL;
		/* 取出EG里面的一些旧值，操作过程在yaf_loader.h */
		YAF_STORE_EG_ENVIRON();

		EG(return_value_ptr_ptr) = &result;
		EG(active_op_array) 	 = op_array;

#if ((PHP_MAJOR_VERSION == 5) && (PHP_MINOR_VERSION > 2)) || (PHP_MAJOR_VERSION > 5)
		if (!EG(active_symbol_table)) {
			zend_rebuild_symbol_table(TSRMLS_C);
		}
#endif
		/* 执行op_code */
		zend_execute(op_array TSRMLS_CC);
		/* 执行完毕，销毁op_code数组 */
		destroy_op_array(op_array TSRMLS_CC);
		/* 释放内存 */
		efree(op_array);
		if (!EG(exception)) {	/* 没有异常 */
			if (EG(return_value_ptr_ptr) && *EG(return_value_ptr_ptr)) {
				zval_ptr_dtor(EG(return_value_ptr_ptr));
			}
		}
		/* 将新值放入EG */
		YAF_RESTORE_EG_ENVIRON();
	    return 1;
	}

	return 0;
}
/* }}} */

/** {{{ int yaf_internal_autoload(char * file_name, uint name_len, char **directory TSRMLS_DC)
 */
int yaf_internal_autoload(char *file_name, uint name_len, char **directory TSRMLS_DC) {
	zval *library_dir, *global_dir;
	char *q, *p, *seg;
	uint seg_len, directory_len, status;
	char *ext = YAF_G(ext);
	smart_str buf = {0};

	if (NULL == *directory) {
		char *library_path;
		uint  library_path_len;
		yaf_loader_t *loader;

		loader = yaf_loader_instance(NULL, NULL, NULL TSRMLS_CC);

		if (!loader) {
			/* since only call from userspace can cause loader is NULL, exception throw will works well */
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s need to be initialize first", yaf_loader_ce->name);
			return 0;
		} else {
			library_dir = zend_read_property(yaf_loader_ce, loader, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_LIBRARY), 1 TSRMLS_CC);
			global_dir	= zend_read_property(yaf_loader_ce, loader, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_GLOBAL_LIB), 1 TSRMLS_CC);

			if (yaf_loader_is_local_namespace(loader, file_name, name_len TSRMLS_CC)) {
				library_path = Z_STRVAL_P(library_dir);
				library_path_len = Z_STRLEN_P(library_dir);
			} else {
				library_path = Z_STRVAL_P(global_dir);
				library_path_len = Z_STRLEN_P(global_dir);
			}
		}

		if (NULL == library_path) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s requires %s(which set the library_directory) to be initialized first", yaf_loader_ce->name, yaf_application_ce->name);
			return 0;
		}

		smart_str_appendl(&buf, library_path, library_path_len);
	} else {
		smart_str_appendl(&buf, *directory, strlen(*directory));
		efree(*directory);
	}

	directory_len = buf.len;

	/* aussume all the path is not end in slash */
	smart_str_appendc(&buf, DEFAULT_SLASH);

	p = file_name;
	q = p;

	while (1) {
		while(++q && *q != '_' && *q != '\0');

		if (*q != '\0') {
			seg_len	= q - p;
			seg	 	= estrndup(p, seg_len);
			smart_str_appendl(&buf, seg, seg_len);
			efree(seg);
			smart_str_appendc(&buf, DEFAULT_SLASH);
			p 		= q + 1;
		} else {
			break;
		}
	}

	if (YAF_G(lowcase_path)) {
		/* all path of library is lowercase */
		zend_str_tolower(buf.c + directory_len, buf.len - directory_len);
	}

	smart_str_appendl(&buf, p, strlen(p));
	smart_str_appendc(&buf, '.');
	smart_str_appendl(&buf, ext, strlen(ext));

	smart_str_0(&buf);

	if (directory) {
		*(directory) = estrndup(buf.c, buf.len);
	}

	status = yaf_loader_import(buf.c, buf.len, 0 TSRMLS_CC);
	smart_str_free(&buf);

	if (!status)
	   	return 0;

	return 1;
}
/* }}} */

/** {{{ int yaf_loader_register_namespace_single(char *prefix, uint len TSRMLS_DC)
 *	注册单个本地类前缀
 */
int yaf_loader_register_namespace_single(char *prefix, uint len TSRMLS_DC) {

	if (YAF_G(local_namespaces)) {
		/* 已经有值或者初始化 */

		uint orig_len = strlen(YAF_G(local_namespaces));
		/** 
		 *	重新申请内存，并将prefix接在后面
		 *	形式:$local_namespaces . DEFAULT_DIR_SEPARATOR . prefix
		 */
		YAF_G(local_namespaces) = erealloc(YAF_G(local_namespaces), orig_len + 1 + len + 1);
		snprintf(YAF_G(local_namespaces) + orig_len, len + 2, "%c%s", DEFAULT_DIR_SEPARATOR, prefix);
	} else {
		/* 未初始化，直接申请内存，将前缀的字符串值直接格式化后复制给YAF_G(local_namespaces) */
		YAF_G(local_namespaces) = emalloc(len + 1 + 1);
		snprintf(YAF_G(local_namespaces), len + 2, "%s", prefix);
	}

	return 1;
}
/* }}} */

/** {{{ int yaf_loader_register_namespace_multi(zval *prefixes TSRMLS_DC)
 *	设置多个本地类前缀
 */
int yaf_loader_register_namespace_multi(zval *prefixes TSRMLS_DC) {
	zval **ppzval;
	HashTable *ht;
	/* 传进来的是一个数组 */
	ht = Z_ARRVAL_P(prefixes);
	/* 遍历获取数组里面存的每一个前缀，利用yaf_loader_register_namespace_single一个个注册 */
	for(zend_hash_internal_pointer_reset(ht);
			zend_hash_has_more_elements(ht) == SUCCESS;
			zend_hash_move_forward(ht)) {
		if (zend_hash_get_current_data(ht, (void**)&ppzval) == FAILURE) {
			continue;
		} else if (IS_STRING == Z_TYPE_PP(ppzval)) {
			yaf_loader_register_namespace_single(Z_STRVAL_PP(ppzval), Z_STRLEN_PP(ppzval) TSRMLS_CC);
		}
	}

	return 1;
}
/* }}} */

/** {{{ proto private Yaf_Loader::__construct(void)
*/
PHP_METHOD(yaf_loader, __construct) {
}
/* }}} */

/** {{{ proto private Yaf_Loader::__sleep(void)
*/
PHP_METHOD(yaf_loader, __sleep) {
}
/* }}} */

/** {{{ proto private Yaf_Loader::__wakeup(void)
*/
PHP_METHOD(yaf_loader, __wakeup) {
}
/* }}} */

/** {{{ proto private Yaf_Loader::__clone(void)
*/
PHP_METHOD(yaf_loader, __clone) {
}
/* }}} */

/** {{{ proto public Yaf_Loader::registerLocalNamespace(mixed $namespace)
*/
PHP_METHOD(yaf_loader, registerLocalNamespace) {
	zval *namespaces;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &namespaces) == FAILURE) {
		return;
	}

	if (IS_STRING == Z_TYPE_P(namespaces)) {
		/* 传进来的变量类型为字符串则注册单个类前缀 */
		if (yaf_loader_register_namespace_single(Z_STRVAL_P(namespaces), Z_STRLEN_P(namespaces) TSRMLS_CC)) {
			/* return $this */
			RETURN_ZVAL(getThis(), 1, 0);
		}
	} else if (IS_ARRAY == Z_TYPE_P(namespaces)) {
		/* 传进来的变量类型为数组则注册多个类前缀 */
		if(yaf_loader_register_namespace_multi(namespaces TSRMLS_CC)) {
			/* return $this */
			RETURN_ZVAL(getThis(), 1, 0);
		}
	} else {
		/* 其他类型直接报错 */
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid parameters provided, must be a string, or an array");
	}

	RETURN_FALSE;
}
/* }}} */

/** {{{ proto public Yaf_Loader::getLocalNamespace(void)
 *	获取当前已经注册的本地类前缀
*/
PHP_METHOD(yaf_loader, getLocalNamespace) {
	if (YAF_G(local_namespaces)) {
		RETURN_STRING(YAF_G(local_namespaces), 1);
	}
	RETURN_NULL();
}
/* }}} */

/** {{{ proto public Yaf_Loader::clearLocalNamespace(void)
 *	清除已注册的本地类前缀
*/
PHP_METHOD(yaf_loader, clearLocalNamespace) {
	/* 释放YAF_G(local_namespaces) */
	efree(YAF_G(local_namespaces));
	YAF_G(local_namespaces) = NULL;

	RETURN_TRUE;
}
/* }}} */

/** {{{ proto public Yaf_Loader::isLocalName(string $class_name)
 *	判断一个类, 是否是本地类.
*/
PHP_METHOD(yaf_loader, isLocalName) {
	zval *name;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &name) == FAILURE) {
		return;
	}
	/* 变量类型不是字符串，返回false */
	if (Z_TYPE_P(name) != IS_STRING) {
		RETURN_FALSE;
	}

	RETURN_BOOL(yaf_loader_is_local_namespace(getThis(), Z_STRVAL_P(name), Z_STRLEN_P(name) TSRMLS_CC));
}
/* }}} */

/** {{{ proto public Yaf_Loader::setLibraryPath(string $path, $global = FALSE)
 *	设置类库地址
*/
PHP_METHOD(yaf_loader, setLibraryPath) {
	char *library;
	uint len;
	zend_bool global = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|b", &library, &len, &global) == FAILURE) {
		return;
	}

	if (!global) {
		/* 设置成本地类库 */
		zend_update_property_stringl(yaf_loader_ce, getThis(), ZEND_STRL(YAF_LOADER_PROPERTY_NAME_LIBRARY), library, len TSRMLS_CC);
	} else {
		/* 设置成全局类库 */
		zend_update_property_stringl(yaf_loader_ce, getThis(), ZEND_STRL(YAF_LOADER_PROPERTY_NAME_GLOBAL_LIB), library, len TSRMLS_CC);
	}
	/* return $this */
	RETURN_ZVAL(getThis(), 1, 0);
}
/* }}} */

/** {{{ proto public Yaf_Loader::getLibraryPath($global = FALSE)
 *	获取类库地址
*/
PHP_METHOD(yaf_loader, getLibraryPath) {
	zval *library;
	zend_bool global = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|b", &global) == FAILURE) {
		return;
	}

	if (!global) {
		/* 获取本地类库地址 */		
		library = zend_read_property(yaf_loader_ce, getThis(), ZEND_STRL(YAF_LOADER_PROPERTY_NAME_LIBRARY), 1 TSRMLS_CC);
	} else {
		/* 获取全局类库地址 */	
		library = zend_read_property(yaf_loader_ce, getThis(), ZEND_STRL(YAF_LOADER_PROPERTY_NAME_GLOBAL_LIB), 1 TSRMLS_CC);
	}

	RETURN_ZVAL(library, 1, 0);
}
/* }}} */

/** {{{ proto public static Yaf_Loader::import($file)
 *	导入一个PHP文件, 因为Yaf_Loader::import只是专注于一次包含, 所以要比传统的require_once性能好一些
*/
PHP_METHOD(yaf_loader, import) {
	char *file;
	uint len, need_free = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s" ,&file, &len) == FAILURE) {
		return;
	}

	if (!len) {
		/* 没传name返回false */
		RETURN_FALSE;
	} else {
		int  retval = 0;

		if (!IS_ABSOLUTE_PATH(file, len)) {
			/* 不是据对路径 */

			yaf_loader_t *loader = yaf_loader_instance(NULL, NULL, NULL TSRMLS_CC);		/* 获取类的实例 */
			if (!loader) {
				/* 获取不到报错 */
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s need to be initialize first", yaf_loader_ce->name);
				RETURN_FALSE;
			} else {
				/* 获取本地类库 */
				zval *library = zend_read_property(yaf_loader_ce, loader, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_LIBRARY), 1 TSRMLS_CC);
				/* 利用本地类库为传递进来的相对路径拼出绝对路径 */
				len = spprintf(&file, 0, "%s%c%s", Z_STRVAL_P(library), DEFAULT_SLASH, file);
				need_free = 1;
			}
		}
		/* 判断EG(included_files)中是否已经存了文件名 */
		retval = (zend_hash_exists(&EG(included_files), file, len + 1));
		/* 如果已经存了，释放保存文件名的内存，返回true */
		if (retval) {
			if (need_free) {
				efree(file);
			}
			RETURN_TRUE;
		}
		/* 加载文件，释放保存文件名的字符串内存 */
		retval = yaf_loader_import(file, len, 0 TSRMLS_CC);
		if (need_free) {
			efree(file);
		}
		/* 加载成功返回true,加载失败返回false */
		RETURN_BOOL(retval);
	}
}
/* }}} */

/** {{{ proto public Yaf_Loader::autoload($class_name)
*/
PHP_METHOD(yaf_loader, autoload) {
	char *class_name, *origin_classname, *app_directory, *directory = NULL, *file_name = NULL;
#ifdef YAF_HAVE_NAMESPACE
	char *origin_lcname = NULL;
#endif
	uint separator_len, class_name_len, file_name_len = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &class_name, &class_name_len) == FAILURE) {
		return;
	}

	separator_len = YAF_G(name_separator_len);
	app_directory = YAF_G(directory);
	origin_classname = class_name;

	do {
		if (!class_name_len) {
			break;
		}
#ifdef YAF_HAVE_NAMESPACE
		/* 使用的是namespace的话，这里做的工作就是将类名里面的\\转换成下划线_ */
		{
			int pos = 0;
			origin_lcname = estrndup(class_name, class_name_len);
			class_name 	  = origin_lcname;
			while (pos < class_name_len) {
				if (*(class_name + pos) == '\\') {
					*(class_name + pos) = '_';
				}
				pos += 1;
			}
		}
#endif
		/* 用户定义的类名不能使用Yaf_开头 */
		if (strncmp(class_name, YAF_LOADER_RESERVERD, YAF_LOADER_LEN_RESERVERD) == 0) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "You should not use '%s' as class name prefix", YAF_LOADER_RESERVERD);
		}

		if (yaf_loader_is_category(class_name, class_name_len, YAF_LOADER_MODEL, YAF_LOADER_LEN_MODEL TSRMLS_CC)) {
			/* this is a model class */
			spprintf(&directory, 0, "%s/%s", app_directory, YAF_MODEL_DIRECTORY_NAME);
			file_name_len = class_name_len - separator_len - YAF_LOADER_LEN_MODEL;

			if (YAF_G(name_suffix)) {
				file_name = estrndup(class_name, file_name_len);
			} else {
				file_name = estrdup(class_name + YAF_LOADER_LEN_MODEL + separator_len);
			}

			break;
		}

		if (yaf_loader_is_category(class_name, class_name_len, YAF_LOADER_PLUGIN, YAF_LOADER_LEN_PLUGIN TSRMLS_CC)) {
			/* this is a plugin class */
			spprintf(&directory, 0, "%s/%s", app_directory, YAF_PLUGIN_DIRECTORY_NAME);
			file_name_len = class_name_len - separator_len - YAF_LOADER_LEN_PLUGIN;

			if (YAF_G(name_suffix)) {
				file_name = estrndup(class_name, file_name_len);
			} else {
				file_name = estrdup(class_name + YAF_LOADER_LEN_PLUGIN + separator_len);
			}

			break;
		}

		if (yaf_loader_is_category(class_name, class_name_len, YAF_LOADER_CONTROLLER, YAF_LOADER_LEN_CONTROLLER TSRMLS_CC)) {
			/* this is a controller class */
			spprintf(&directory, 0, "%s/%s", app_directory, YAF_CONTROLLER_DIRECTORY_NAME);
			file_name_len = class_name_len - separator_len - YAF_LOADER_LEN_CONTROLLER;

			if (YAF_G(name_suffix)) {
				file_name = estrndup(class_name, file_name_len);
			} else {
				file_name = estrdup(class_name + YAF_LOADER_LEN_CONTROLLER + separator_len);
			}

			break;
		}


/* {{{ This only effects internally */
		if (YAF_G(st_compatible) && (strncmp(class_name, YAF_LOADER_DAO, YAF_LOADER_LEN_DAO) == 0
					|| strncmp(class_name, YAF_LOADER_SERVICE, YAF_LOADER_LEN_SERVICE) == 0)) {
			/* this is a model class */
			spprintf(&directory, 0, "%s/%s", app_directory, YAF_MODEL_DIRECTORY_NAME);
		}
/* }}} */

		file_name_len = class_name_len;
		file_name     = class_name;

	} while(0);

	if (!app_directory && directory) {
		efree(directory);
#ifdef YAF_HAVE_NAMESPACE
		if (origin_lcname) {
			efree(origin_lcname);
		}
#endif
		if (file_name != class_name) {
			efree(file_name);
		}

		php_error_docref(NULL TSRMLS_CC, E_WARNING,
				"Couldn't load a framework MVC class without an %s initializing", yaf_application_ce->name);
		RETURN_FALSE;
	}

	if (!YAF_G(use_spl_autoload)) {
		/** directory might be NULL since we passed a NULL */
		if (yaf_internal_autoload(file_name, file_name_len, &directory TSRMLS_CC)) {
			char *lc_classname = zend_str_tolower_dup(origin_classname, class_name_len);
			if (zend_hash_exists(EG(class_table), lc_classname, class_name_len + 1)) {
#ifdef YAF_HAVE_NAMESPACE
				if (origin_lcname) {
					efree(origin_lcname);
				}
#endif
				if (directory) {
					efree(directory);
				}

				if (file_name != class_name) {
					efree(file_name);
				}

				efree(lc_classname);
				RETURN_TRUE;
			} else {
				efree(lc_classname);
				php_error_docref(NULL TSRMLS_CC, E_STRICT, "Could not find class %s in %s", class_name, directory);
			}
		}  else {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed opening script %s: %s", directory, strerror(errno));
		}

#ifdef YAF_HAVE_NAMESPACE
		if (origin_lcname) {
			efree(origin_lcname);
		}
#endif
		if (directory) {
			efree(directory);
		}
		if (file_name != class_name) {
			efree(file_name);
		}
		RETURN_TRUE;
	} else {
		char *lower_case_name = zend_str_tolower_dup(origin_classname, class_name_len);
		if (yaf_internal_autoload(file_name, file_name_len, &directory TSRMLS_CC) &&
				zend_hash_exists(EG(class_table), lower_case_name, class_name_len + 1)) {
#ifdef YAF_HAVE_NAMESPACE
			if (origin_lcname) {
				efree(origin_lcname);
			}
#endif
			if (directory) {
				efree(directory);
			}
			if (file_name != class_name) {
				efree(file_name);
			}

			efree(lower_case_name);
			RETURN_TRUE;
		}
#ifdef YAF_HAVE_NAMESPACE
		if (origin_lcname) {
			efree(origin_lcname);
		}
#endif
		if (directory) {
			efree(directory);
		}
		if (file_name != class_name) {
			efree(file_name);
		}
		efree(lower_case_name);
		RETURN_FALSE;
	}
}
/* }}} */

/** {{{ proto public Yaf_Loader::getInstance($library = NULL, $global_library = NULL)
 *	返回类的实例，手册里面写的这里没有参数，但是这里可以传入两个参数
 */
PHP_METHOD(yaf_loader, getInstance) {
	char *library	 	= NULL;
	char *global	 	= NULL;
	int	 library_len 	= 0;
	int  global_len	 	= 0;
	yaf_loader_t *loader;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|ss", &library, &library_len, &global, &global_len) == FAILURE) {
		return;
	} 

	loader = yaf_loader_instance(NULL, library, global TSRMLS_CC);
	if (loader)
		RETURN_ZVAL(loader, 1, 0);

	RETURN_FALSE;
}
/* }}} */

/** {{{ proto private Yaf_Loader::__desctruct(void)
*/
PHP_METHOD(yaf_loader, __destruct) {
}
/* }}} */

/** {{{ proto yaf_override_spl_autoload($class_name)
 *	文档对这个也没介绍，估计是历史遗漏吧
*/
PHP_FUNCTION(yaf_override_spl_autoload) {
	php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s is disabled by ap.use_spl_autoload", YAF_SPL_AUTOLOAD_REGISTER_NAME);
	RETURN_BOOL(0);
}
/* }}} */

/** {{{ yaf_loader_methods
*/
zend_function_entry yaf_loader_methods[] = {
	PHP_ME(yaf_loader, __construct, 			yaf_loader_void_arginfo, ZEND_ACC_PRIVATE|ZEND_ACC_CTOR)
	PHP_ME(yaf_loader, __clone,					NULL, ZEND_ACC_PRIVATE|ZEND_ACC_CLONE)
	PHP_ME(yaf_loader, __sleep,					NULL, ZEND_ACC_PRIVATE)
	PHP_ME(yaf_loader, __wakeup,				NULL, ZEND_ACC_PRIVATE)
	PHP_ME(yaf_loader, autoload,				yaf_loader_autoloader_arginfo,  ZEND_ACC_PUBLIC)
	PHP_ME(yaf_loader, getInstance,				yaf_loader_getinstance_arginfo, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	PHP_ME(yaf_loader, registerLocalNamespace,	yaf_loader_regnamespace_arginfo, ZEND_ACC_PUBLIC)
	PHP_ME(yaf_loader, getLocalNamespace,		yaf_loader_void_arginfo, ZEND_ACC_PUBLIC)
	PHP_ME(yaf_loader, clearLocalNamespace,		yaf_loader_void_arginfo, ZEND_ACC_PUBLIC)
	PHP_ME(yaf_loader, isLocalName,				yaf_loader_islocalname_arginfo, ZEND_ACC_PUBLIC)
	PHP_ME(yaf_loader, import,					yaf_loader_import_arginfo, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	PHP_ME(yaf_loader, setLibraryPath,			yaf_loader_setlib_arginfo, ZEND_ACC_PUBLIC)
	PHP_ME(yaf_loader, getLibraryPath,			yaf_loader_getlib_arginfo, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};
/* }}} */

/** {{{ YAF_STARTUP_FUNCTION
*/
YAF_STARTUP_FUNCTION(loader) {
	zend_class_entry ce;

	YAF_INIT_CLASS_ENTRY(ce, "Yaf_Loader",  "Yaf\\Loader", yaf_loader_methods);
	yaf_loader_ce = zend_register_internal_class_ex(&ce, NULL, NULL TSRMLS_CC);
	/* final class Yaf_Loader */
	yaf_loader_ce->ce_flags |= ZEND_ACC_FINAL_CLASS;

	/**
	 *	protected $_library = null;
	 *	protected $_global_library = null;
	 *	protected static $_instance = null;
	 */
	zend_declare_property_null(yaf_loader_ce, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_LIBRARY), 	 ZEND_ACC_PROTECTED TSRMLS_CC);
	zend_declare_property_null(yaf_loader_ce, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_GLOBAL_LIB), ZEND_ACC_PROTECTED TSRMLS_CC);
	zend_declare_property_null(yaf_loader_ce, ZEND_STRL(YAF_LOADER_PROPERTY_NAME_INSTANCE),	 ZEND_ACC_PROTECTED|ZEND_ACC_STATIC TSRMLS_CC);

	return SUCCESS;
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
