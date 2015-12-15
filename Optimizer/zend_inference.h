/*
   +----------------------------------------------------------------------+
   | Zend Engine, e-SSA based Type & Range Inference                      |
   +----------------------------------------------------------------------+
   | Copyright (c) 1998-2015 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Dmitry Stogov <dmitry@zend.com>                             |
   +----------------------------------------------------------------------+
*/

#ifndef ZEND_INFERENCE_H
#define ZEND_INFERENCE_H

#include "zend_optimizer.h"
#include "zend_ssa.h"
#include "zend_bitset.h"

/* Bitmask for type inference (zend_ssa_var_info.type) */
#define MAY_BE_UNDEF                (1 << IS_UNDEF)
#define MAY_BE_NULL		            (1 << IS_NULL)
#define MAY_BE_FALSE	            (1 << IS_FALSE)
#define MAY_BE_TRUE		            (1 << IS_TRUE)
#define MAY_BE_LONG		            (1 << IS_LONG)
#define MAY_BE_DOUBLE	            (1 << IS_DOUBLE)
#define MAY_BE_STRING	            (1 << IS_STRING)
#define MAY_BE_ARRAY	            (1 << IS_ARRAY)
#define MAY_BE_OBJECT	            (1 << IS_OBJECT)
#define MAY_BE_RESOURCE	            (1 << IS_RESOURCE)
#define MAY_BE_ANY                  (MAY_BE_NULL|MAY_BE_FALSE|MAY_BE_TRUE|MAY_BE_LONG|MAY_BE_DOUBLE|MAY_BE_STRING|MAY_BE_ARRAY|MAY_BE_OBJECT|MAY_BE_RESOURCE)
#define MAY_BE_REF                  (1 << IS_REFERENCE) /* may be reference */

#define MAY_BE_ARRAY_SHIFT          (IS_REFERENCE)

#define MAY_BE_ARRAY_OF_NULL		(MAY_BE_NULL     << MAY_BE_ARRAY_SHIFT)
#define MAY_BE_ARRAY_OF_FALSE		(MAY_BE_FALSE    << MAY_BE_ARRAY_SHIFT)
#define MAY_BE_ARRAY_OF_TRUE		(MAY_BE_TRUE     << MAY_BE_ARRAY_SHIFT)
#define MAY_BE_ARRAY_OF_LONG		(MAY_BE_LONG     << MAY_BE_ARRAY_SHIFT)
#define MAY_BE_ARRAY_OF_DOUBLE		(MAY_BE_DOUBLE   << MAY_BE_ARRAY_SHIFT)
#define MAY_BE_ARRAY_OF_STRING		(MAY_BE_STRING   << MAY_BE_ARRAY_SHIFT)
#define MAY_BE_ARRAY_OF_ARRAY		(MAY_BE_ARRAY    << MAY_BE_ARRAY_SHIFT)
#define MAY_BE_ARRAY_OF_OBJECT		(MAY_BE_OBJECT   << MAY_BE_ARRAY_SHIFT)
#define MAY_BE_ARRAY_OF_RESOURCE	(MAY_BE_RESOURCE << MAY_BE_ARRAY_SHIFT)
#define MAY_BE_ARRAY_OF_ANY			(MAY_BE_ANY      << MAY_BE_ARRAY_SHIFT)
#define MAY_BE_ARRAY_OF_REF			(MAY_BE_REF      << MAY_BE_ARRAY_SHIFT)

#define MAY_BE_ARRAY_KEY_LONG       (1<<21)
#define MAY_BE_ARRAY_KEY_STRING     (1<<22)
#define MAY_BE_ARRAY_KEY_ANY        (MAY_BE_ARRAY_KEY_LONG | MAY_BE_ARRAY_KEY_STRING)

#define MAY_BE_ERROR                (1<<23)
#define MAY_BE_CLASS                (1<<24)

#define MAY_BE_IN_REG               (1<<25) /* value allocated in CPU register */

//TODO: remome MAY_BE_DEF, MAY_BE_RC1, MAY_BE_RCN???
#define MAY_BE_DEF                  (1<<26)
#define MAY_BE_RC1                  (1<<27) /* may be non-reference with refcount == 1 */
#define MAY_BE_RCN                  (1<<28) /* may be non-reference with refcount > 1  */


#define DEFINE_SSA_OP_HAS_RANGE(opN) \
	static zend_always_inline long _ssa_##opN##_has_range(const zend_op_array *op_array, const zend_ssa *ssa, const zend_op *opline) \
	{ \
		if (opline->opN##_type == IS_CONST) { \
			zval *zv = CRT_CONSTANT_EX(op_array, opline->opN, ssa->rt_constants); \
			return (Z_TYPE_P(zv) == IS_LONG || Z_TYPE_P(zv) == IS_TRUE || Z_TYPE_P(zv) == IS_FALSE || Z_TYPE_P(zv) == IS_NULL); \
		} else { \
			return (opline->opN##_type != IS_UNUSED && \
		        ssa->ops && \
		        ssa->var_info && \
		        ssa->ops[opline - op_array->opcodes].opN##_use >= 0 && \
			    ssa->var_info[ssa->ops[opline - op_array->opcodes].opN##_use].has_range); \
		} \
		return 0; \
	}

#define DEFINE_SSA_OP_MIN_RANGE(opN) \
	static zend_always_inline long _ssa_##opN##_min_range(const zend_op_array *op_array, const zend_ssa *ssa, const zend_op *opline) \
	{ \
		if (opline->opN##_type == IS_CONST) { \
			zval *zv = CRT_CONSTANT_EX(op_array, opline->opN, ssa->rt_constants); \
			if (Z_TYPE_P(zv) == IS_LONG) { \
				return Z_LVAL_P(zv); \
			} else if (Z_TYPE_P(zv) == IS_TRUE) { \
				return 1; \
			} else if (Z_TYPE_P(zv) == IS_FALSE) { \
				return 0; \
			} else if (Z_TYPE_P(zv) == IS_NULL) { \
				return 0; \
			} \
		} else if (opline->opN##_type != IS_UNUSED && \
		    ssa->ops && \
		    ssa->var_info && \
		    ssa->ops[opline - op_array->opcodes].opN##_use >= 0 && \
		    ssa->var_info[ssa->ops[opline - op_array->opcodes].opN##_use].has_range) { \
			return ssa->var_info[ssa->ops[opline - op_array->opcodes].opN##_use].range.min; \
		} \
		return LONG_MIN; \
	}

#define DEFINE_SSA_OP_MAX_RANGE(opN) \
	static zend_always_inline long _ssa_##opN##_max_range(const zend_op_array *op_array, const zend_ssa *ssa, const zend_op *opline) \
	{ \
		if (opline->opN##_type == IS_CONST) { \
			zval *zv = CRT_CONSTANT_EX(op_array, opline->opN, ssa->rt_constants); \
			if (Z_TYPE_P(zv) == IS_LONG) { \
				return Z_LVAL_P(zv); \
			} else if (Z_TYPE_P(zv) == IS_TRUE) { \
				return 1; \
			} else if (Z_TYPE_P(zv) == IS_FALSE) { \
				return 0; \
			} else if (Z_TYPE_P(zv) == IS_NULL) { \
				return 0; \
			} \
		} else if (opline->opN##_type != IS_UNUSED && \
		    ssa->ops && \
		    ssa->var_info && \
		    ssa->ops[opline - op_array->opcodes].opN##_use >= 0 && \
		    ssa->var_info[ssa->ops[opline - op_array->opcodes].opN##_use].has_range) { \
			return ssa->var_info[ssa->ops[opline - op_array->opcodes].opN##_use].range.max; \
		} \
		return LONG_MAX; \
	}

#define DEFINE_SSA_OP_RANGE_UNDERFLOW(opN) \
	static zend_always_inline char _ssa_##opN##_range_underflow(const zend_op_array *op_array, const zend_ssa *ssa, const zend_op *opline) \
	{ \
		if (opline->opN##_type == IS_CONST) { \
			zval *zv = CRT_CONSTANT_EX(op_array, opline->opN, ssa->rt_constants); \
			if (Z_TYPE_P(zv) == IS_LONG || Z_TYPE_P(zv) == IS_TRUE || Z_TYPE_P(zv) == IS_FALSE || Z_TYPE_P(zv) == IS_NULL) { \
				return 0; \
			} \
		} else if (opline->opN##_type != IS_UNUSED && \
		    ssa->ops && \
		    ssa->var_info && \
		    ssa->ops[opline - op_array->opcodes].opN##_use >= 0 && \
		    ssa->var_info[ssa->ops[opline - op_array->opcodes].opN##_use].has_range) { \
			return ssa->var_info[ssa->ops[opline - op_array->opcodes].opN##_use].range.underflow; \
		} \
		return 1; \
	}

#define DEFINE_SSA_OP_RANGE_OVERFLOW(opN) \
	static zend_always_inline char _ssa_##opN##_range_overflow(const zend_op_array *op_array, const zend_ssa *ssa, const zend_op *opline) \
	{ \
		if (opline->opN##_type == IS_CONST) { \
			zval *zv = CRT_CONSTANT_EX(op_array, opline->opN, ssa->rt_constants); \
			if (Z_TYPE_P(zv) == IS_LONG || Z_TYPE_P(zv) == IS_TRUE || Z_TYPE_P(zv) == IS_FALSE || Z_TYPE_P(zv) == IS_NULL) { \
				return 0; \
			} \
		} else if (opline->opN##_type != IS_UNUSED && \
		    ssa->ops && \
		    ssa->var_info && \
		    ssa->ops[opline - op_array->opcodes].opN##_use >= 0 && \
		    ssa->var_info[ssa->ops[opline - op_array->opcodes].opN##_use].has_range) { \
			return ssa->var_info[ssa->ops[opline - op_array->opcodes].opN##_use].range.overflow; \
		} \
		return 1; \
	}

DEFINE_SSA_OP_HAS_RANGE(op1)
DEFINE_SSA_OP_MIN_RANGE(op1)
DEFINE_SSA_OP_MAX_RANGE(op1)
DEFINE_SSA_OP_RANGE_UNDERFLOW(op1)
DEFINE_SSA_OP_RANGE_OVERFLOW(op1)
DEFINE_SSA_OP_HAS_RANGE(op2)
DEFINE_SSA_OP_MIN_RANGE(op2)
DEFINE_SSA_OP_MAX_RANGE(op2)
DEFINE_SSA_OP_RANGE_UNDERFLOW(op2)
DEFINE_SSA_OP_RANGE_OVERFLOW(op2)

#define OP1_HAS_RANGE()         (_ssa_op1_has_range (op_array, ssa, opline))
#define OP1_MIN_RANGE()         (_ssa_op1_min_range (op_array, ssa, opline))
#define OP1_MAX_RANGE()         (_ssa_op1_max_range (op_array, ssa, opline))
#define OP1_RANGE_UNDERFLOW()   (_ssa_op1_range_underflow (op_array, ssa, opline))
#define OP1_RANGE_OVERFLOW()    (_ssa_op1_range_overflow (op_array, ssa, opline))
#define OP2_HAS_RANGE()         (_ssa_op2_has_range (op_array, ssa, opline))
#define OP2_MIN_RANGE()         (_ssa_op2_min_range (op_array, ssa, opline))
#define OP2_MAX_RANGE()         (_ssa_op2_max_range (op_array, ssa, opline))
#define OP2_RANGE_UNDERFLOW()   (_ssa_op2_range_underflow (op_array, ssa, opline))
#define OP2_RANGE_OVERFLOW()    (_ssa_op2_range_overflow (op_array, ssa, opline))

static zend_always_inline uint32_t _const_op_type(const zval *zv) {
	if (Z_TYPE_P(zv) == IS_CONSTANT) {
		return MAY_BE_ANY | MAY_BE_ARRAY_KEY_ANY | MAY_BE_ARRAY_OF_ANY;
	} else if (Z_TYPE_P(zv) == IS_CONSTANT_AST) {
		return MAY_BE_ANY | MAY_BE_ARRAY_KEY_ANY | MAY_BE_ARRAY_OF_ANY;
	} else if (Z_TYPE_P(zv) == IS_ARRAY) {
		HashTable *ht = Z_ARRVAL_P(zv);
		uint32_t tmp = MAY_BE_ARRAY | MAY_BE_DEF | MAY_BE_RC1;

		zend_string *str;
		zval *val;
		ZEND_HASH_FOREACH_STR_KEY_VAL(ht, str, val) {
			if (str) {
				tmp |= MAY_BE_ARRAY_KEY_STRING;
			} else {
				tmp |= MAY_BE_ARRAY_KEY_LONG;
			}
			tmp |= 1 << (Z_TYPE_P(val) + MAY_BE_ARRAY_SHIFT);
		} ZEND_HASH_FOREACH_END();
		return tmp;
	} else {
		return (1 << Z_TYPE_P(zv)) | MAY_BE_DEF | MAY_BE_RC1;
	}
}

static zend_always_inline uint32_t get_ssa_var_info(const zend_ssa *ssa, int ssa_var_num)
{
	if (ssa->var_info && ssa_var_num >= 0) {
		return ssa->var_info[ssa_var_num].type;
	} else {
		return MAY_BE_DEF | MAY_BE_UNDEF | MAY_BE_RC1 | MAY_BE_RCN | MAY_BE_REF | MAY_BE_ANY | MAY_BE_ARRAY_KEY_ANY | MAY_BE_ARRAY_OF_ANY | MAY_BE_ARRAY_OF_REF | MAY_BE_ERROR;
	}
}

#define DEFINE_SSA_OP_INFO(opN) \
	static zend_always_inline uint32_t _ssa_##opN##_info(const zend_op_array *op_array, const zend_ssa *ssa, const zend_op *opline) \
	{																		\
		if (opline->opN##_type == IS_CONST) {							\
			return _const_op_type(CRT_CONSTANT_EX(op_array, opline->opN, ssa->rt_constants)); \
		} else { \
			return get_ssa_var_info(ssa, ssa->ops ? ssa->ops[opline - op_array->opcodes].opN##_use : -1); \
		} \
	}

#define DEFINE_SSA_OP_DEF_INFO(opN) \
	static zend_always_inline uint32_t _ssa_##opN##_def_info(const zend_op_array *op_array, const zend_ssa *ssa, const zend_op *opline) \
	{ \
		return get_ssa_var_info(ssa, ssa->ops ? ssa->ops[opline - op_array->opcodes].opN##_def : -1); \
	}


DEFINE_SSA_OP_INFO(op1)
DEFINE_SSA_OP_INFO(op2)
DEFINE_SSA_OP_INFO(result)
DEFINE_SSA_OP_DEF_INFO(op1)
DEFINE_SSA_OP_DEF_INFO(op2)
DEFINE_SSA_OP_DEF_INFO(result)

#define OP1_INFO()              (_ssa_op1_info(op_array, ssa, opline))
#define OP2_INFO()              (_ssa_op2_info(op_array, ssa, opline))
#define OP1_DATA_INFO()         (_ssa_op1_info(op_array, ssa, (opline+1)))
#define OP2_DATA_INFO()         (_ssa_op2_info(op_array, ssa, (opline+1)))
#define RES_USE_INFO()          (_ssa_result_info(op_array, ssa, opline))
#define OP1_DEF_INFO()          (_ssa_op1_def_info(op_array, ssa, opline))
#define OP2_DEF_INFO()          (_ssa_op2_def_info(op_array, ssa, opline))
#define OP1_DATA_DEF_INFO()     (_ssa_op1_def_info(op_array, ssa, (opline+1)))
#define OP2_DATA_DEF_INFO()     (_ssa_op2_def_info(op_array, ssa, (opline+1)))
#define RES_INFO()              (_ssa_result_def_info(op_array, ssa, opline))


BEGIN_EXTERN_C()

int zend_ssa_find_false_dependencies(const zend_op_array *op_array, zend_ssa *ssa);
int zend_ssa_find_sccs(const zend_op_array *op_array, zend_ssa *ssa);
int zend_ssa_inference(zend_arena **raena, const zend_op_array *op_array, const zend_script *script, zend_ssa *ssa);

uint32_t zend_array_element_type(uint32_t t1, int write, int insert);

int  zend_inference_calc_range(const zend_op_array *op_array, zend_ssa *ssa, int var, int widening, int narrowing, zend_ssa_range *tmp);
void zend_inference_init_range(const zend_op_array *op_array, zend_ssa *ssa, int var, zend_bool underflow, long min, long max, zend_bool overflow);
int  zend_inference_narrowing_meet(zend_ssa_var_info *var_info, zend_ssa_range *r);
int  zend_inference_widening_meet(zend_ssa_var_info *var_info, zend_ssa_range *r);
void zend_inference_check_recursive_dependencies(zend_op_array *op_array);

int  zend_infer_types_ex(const zend_op_array *op_array, const zend_script *script, zend_ssa *ssa, zend_bitset worklist);

void zend_func_return_info(const zend_op_array   *op_array,
                           int                    recursive,
                           int                    widening,
                           zend_ssa_var_info     *ret);

END_EXTERN_C()

#endif /* ZEND_INFERENCE_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */
