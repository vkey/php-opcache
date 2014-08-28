/*
   +----------------------------------------------------------------------+
   | Zend OPcache                                                         |
   +----------------------------------------------------------------------+
   | Copyright (c) 1998-2014 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Andi Gutmans <andi@zend.com>                                |
   |          Zeev Suraski <zeev@zend.com>                                |
   |          Stanislav Malyshev <stas@zend.com>                          |
   |          Dmitry Stogov <dmitry@zend.com>                             |
   +----------------------------------------------------------------------+
*/

#include "php.h"
#include "Optimizer/zend_optimizer.h"
#include "Optimizer/zend_optimizer_internal.h"
#include "zend_API.h"
#include "zend_constants.h"
#include "zend_execute.h"
#include "zend_vm.h"

static void zend_optimizer_zval_dtor_wrapper(zval *zvalue)
{
	zval_dtor(zvalue);
}

void zend_optimizer_collect_constant(zend_optimizer_ctx *ctx, zval *name, zval* value)
{
	zval val;

	if (!ctx->constants) {
		ctx->constants = zend_arena_alloc(&ctx->arena, sizeof(HashTable));
		zend_hash_init(ctx->constants, 16, NULL, zend_optimizer_zval_dtor_wrapper, 0);
	}
	ZVAL_DUP(&val, value);
	zend_hash_add(ctx->constants, Z_STR_P(name), &val);
}

int zend_optimizer_get_collected_constant(HashTable *constants, zval *name, zval* value)
{
	zval *val;

	if ((val = zend_hash_find(constants, Z_STR_P(name))) != NULL) {
		ZVAL_DUP(value, val);
		return 1;
	}
	return 0;
}

int zend_optimizer_lookup_cv(zend_op_array *op_array, zend_string* name)
{
	int i = 0;
	zend_ulong hash_value = zend_string_hash_val(name);

	while (i < op_array->last_var) {
		if (op_array->vars[i] == name ||
		    (op_array->vars[i]->h == hash_value &&
		     op_array->vars[i]->len == name->len &&
		     memcmp(op_array->vars[i]->val, name->val, name->len) == 0)) {
			return (int)(zend_intptr_t)EX_VAR_NUM_2(NULL, i);
		}
		i++;
	}
	i = op_array->last_var;
	op_array->last_var++;
	op_array->vars = erealloc(op_array->vars, op_array->last_var * sizeof(zend_string*));
	op_array->vars[i] = zend_string_dup(name, 0);

	/* all IS_TMP_VAR and IS_VAR variable numbers have to be adjusted */
	{
		zend_op *opline = op_array->opcodes;
		zend_op *end = opline + op_array->last;
		while (opline < end) {
			if (opline->op1_type & (IS_TMP_VAR|IS_VAR)) {
				opline->op1.var += sizeof(zval);
			}
			if (opline->op2_type & (IS_TMP_VAR|IS_VAR)) {
				opline->op2.var += sizeof(zval);
			}
			if (opline->result_type & (IS_TMP_VAR|IS_VAR)) {
				opline->result.var += sizeof(zval);
			}
			if (opline->opcode == ZEND_DECLARE_INHERITED_CLASS ||
			    opline->opcode == ZEND_DECLARE_INHERITED_CLASS_DELAYED) {
				opline->extended_value += sizeof(zval);
			}
			opline++;
		}
	}
	
	return (int)(zend_intptr_t)EX_VAR_NUM_2(NULL, i);
}

int zend_optimizer_add_literal(zend_op_array *op_array, zval *zv TSRMLS_DC)
{
	int i = op_array->last_literal;
	op_array->last_literal++;
	op_array->literals = (zval*)erealloc(op_array->literals, op_array->last_literal * sizeof(zval));
	ZVAL_COPY_VALUE(&op_array->literals[i], zv);
	Z_CACHE_SLOT(op_array->literals[i]) = -1;
//???	Z_SET_REFCOUNT(op_array->literals[i].constant, 2);
//???	Z_SET_ISREF(op_array->literals[i].constant);
	return i;
}

void zend_optimizer_update_op1_const(zend_op_array *op_array,
                                     zend_op       *opline,
                                     zval          *val TSRMLS_DC)
{
	if (opline->opcode == ZEND_FREE) {
		MAKE_NOP(opline);
		zval_dtor(val);
	} else {
		ZEND_OP1_TYPE(opline) = IS_CONST;
		if (Z_TYPE_P(val) == IS_STRING) {
			switch (opline->opcode) {
				case ZEND_INIT_STATIC_METHOD_CALL:
				case ZEND_CATCH:
				case ZEND_FETCH_CONSTANT:
				case ZEND_DEFINED:
					opline->op1.constant = zend_optimizer_add_literal(op_array, val TSRMLS_CC);
					zend_string_hash_val(Z_STR(ZEND_OP1_LITERAL(opline)));
					Z_CACHE_SLOT(op_array->literals[opline->op1.constant]) = op_array->last_cache_slot++;
					zend_str_tolower(Z_STRVAL_P(val), Z_STRLEN_P(val));
					zend_optimizer_add_literal(op_array, val TSRMLS_CC);
					zend_string_hash_val(Z_STR(op_array->literals[opline->op1.constant+1]));
					break;
				default:
					opline->op1.constant = zend_optimizer_add_literal(op_array, val TSRMLS_CC);
					zend_string_hash_val(Z_STR(ZEND_OP1_LITERAL(opline)));
					break;
			}
		} else {
			opline->op1.constant = zend_optimizer_add_literal(op_array, val TSRMLS_CC);
		}
	}
}

void zend_optimizer_update_op2_const(zend_op_array *op_array,
                                     zend_op       *opline,
                                     zval          *val TSRMLS_DC)
{
	ZEND_OP2_TYPE(opline) = IS_CONST;
	if (opline->opcode == ZEND_INIT_FCALL) {
		zend_str_tolower(Z_STRVAL_P(val), Z_STRLEN_P(val));
		opline->op2.constant = zend_optimizer_add_literal(op_array, val TSRMLS_CC);
		zend_string_hash_val(Z_STR(ZEND_OP2_LITERAL(opline)));
		Z_CACHE_SLOT(op_array->literals[opline->op2.constant]) = op_array->last_cache_slot++;
		return;
	}
	opline->op2.constant = zend_optimizer_add_literal(op_array, val TSRMLS_CC);
	if (Z_TYPE_P(val) == IS_STRING) {
		zend_string_hash_val(Z_STR(ZEND_OP2_LITERAL(opline)));
		switch (opline->opcode) {
			case ZEND_FETCH_R:
			case ZEND_FETCH_W:
			case ZEND_FETCH_RW:
			case ZEND_FETCH_IS:
			case ZEND_FETCH_UNSET:
			case ZEND_FETCH_FUNC_ARG:
			case ZEND_FETCH_CLASS:
			case ZEND_INIT_FCALL_BY_NAME:
			/*case ZEND_INIT_NS_FCALL_BY_NAME:*/
			case ZEND_UNSET_VAR:
			case ZEND_ISSET_ISEMPTY_VAR:
			case ZEND_ADD_INTERFACE:
			case ZEND_ADD_TRAIT:
				Z_CACHE_SLOT(op_array->literals[opline->op2.constant]) = op_array->last_cache_slot++;
				zend_str_tolower(Z_STRVAL_P(val), Z_STRLEN_P(val));
				zend_optimizer_add_literal(op_array, val TSRMLS_CC);
				zend_string_hash_val(Z_STR(op_array->literals[opline->op2.constant+1]));
				break;
			case ZEND_INIT_METHOD_CALL:
			case ZEND_INIT_STATIC_METHOD_CALL:
				zend_str_tolower(Z_STRVAL_P(val), Z_STRLEN_P(val));
				zend_optimizer_add_literal(op_array, val TSRMLS_CC);
				zend_string_hash_val(Z_STR(op_array->literals[opline->op2.constant+1]));
				/* break missing intentionally */						
			/*case ZEND_FETCH_CONSTANT:*/
			case ZEND_ASSIGN_OBJ:
			case ZEND_FETCH_OBJ_R:
			case ZEND_FETCH_OBJ_W:
			case ZEND_FETCH_OBJ_RW:
			case ZEND_FETCH_OBJ_IS:
			case ZEND_FETCH_OBJ_UNSET:
			case ZEND_FETCH_OBJ_FUNC_ARG:
			case ZEND_UNSET_OBJ:
			case ZEND_PRE_INC_OBJ:
			case ZEND_PRE_DEC_OBJ:
			case ZEND_POST_INC_OBJ:
			case ZEND_POST_DEC_OBJ:
			case ZEND_ISSET_ISEMPTY_PROP_OBJ:
				Z_CACHE_SLOT(op_array->literals[opline->op2.constant]) = op_array->last_cache_slot;
				op_array->last_cache_slot += 2;
				break;
			case ZEND_ASSIGN_ADD:
			case ZEND_ASSIGN_SUB:
			case ZEND_ASSIGN_MUL:
			case ZEND_ASSIGN_DIV:
			case ZEND_ASSIGN_MOD:
			case ZEND_ASSIGN_SL:
			case ZEND_ASSIGN_SR:
			case ZEND_ASSIGN_CONCAT:
			case ZEND_ASSIGN_BW_OR:
			case ZEND_ASSIGN_BW_AND:
			case ZEND_ASSIGN_BW_XOR:
				if (opline->extended_value == ZEND_ASSIGN_OBJ) {
					Z_CACHE_SLOT(op_array->literals[opline->op2.constant]) = op_array->last_cache_slot;
					op_array->last_cache_slot += 2;
				}
				break;
			case ZEND_OP_DATA:
				if ((opline-1)->opcode == ZEND_ASSIGN_DIM ||
				    ((opline-1)->extended_value == ZEND_ASSIGN_DIM &&
				     ((opline-1)->opcode == ZEND_ASSIGN_ADD ||
				     (opline-1)->opcode == ZEND_ASSIGN_SUB ||
				     (opline-1)->opcode == ZEND_ASSIGN_MUL ||
				     (opline-1)->opcode == ZEND_ASSIGN_DIV ||
				     (opline-1)->opcode == ZEND_ASSIGN_MOD ||
				     (opline-1)->opcode == ZEND_ASSIGN_SL ||
				     (opline-1)->opcode == ZEND_ASSIGN_SR ||
				     (opline-1)->opcode == ZEND_ASSIGN_CONCAT ||
				     (opline-1)->opcode == ZEND_ASSIGN_BW_OR ||
				     (opline-1)->opcode == ZEND_ASSIGN_BW_AND ||
				     (opline-1)->opcode == ZEND_ASSIGN_BW_XOR))) {
					goto check_numeric;
				}
				break;
			case ZEND_ISSET_ISEMPTY_DIM_OBJ:
			case ZEND_ADD_ARRAY_ELEMENT:
			case ZEND_INIT_ARRAY:
			case ZEND_UNSET_DIM:
			case ZEND_FETCH_DIM_R:
			case ZEND_FETCH_DIM_W:
			case ZEND_FETCH_DIM_RW:
			case ZEND_FETCH_DIM_IS:
			case ZEND_FETCH_DIM_FUNC_ARG:
			case ZEND_FETCH_DIM_UNSET:
			case ZEND_FETCH_DIM_TMP_VAR:
check_numeric:
				{
					zend_ulong index;

					if (ZEND_HANDLE_NUMERIC(Z_STR_P(val), index)) {
						zval_dtor(val);
						ZVAL_LONG(val, index);
						op_array->literals[opline->op2.constant] = *val;
		        	}
				}
				break;
			default:
				break;
		}
	}
}

int zend_optimizer_replace_var_by_const(zend_op_array *op_array,
                                        zend_op       *opline,
                                        uint32_t      var,
                                        zval          *val TSRMLS_DC)
{
	zend_op *end = op_array->opcodes + op_array->last;

	while (opline < end) {
		if (ZEND_OP1_TYPE(opline) == IS_VAR &&
			ZEND_OP1(opline).var == var) {
			switch (opline->opcode) {
				case ZEND_FETCH_DIM_W:
				case ZEND_FETCH_DIM_RW:
				case ZEND_FETCH_DIM_FUNC_ARG:
				case ZEND_FETCH_DIM_UNSET:
				case ZEND_ASSIGN_DIM:
				case ZEND_SEPARATE:
					return 0;
				case ZEND_SEND_VAR_NO_REF:
					if (opline->extended_value & ZEND_ARG_COMPILE_TIME_BOUND) {
						if (opline->extended_value & ZEND_ARG_SEND_BY_REF) {
							return 0;
						}
						opline->extended_value = 0;
						opline->opcode = ZEND_SEND_VAL_EX;
					} else {
						opline->extended_value = 0;
						opline->opcode = ZEND_SEND_VAL;
					}
					break;
				default:
					break;
			} 
			zend_optimizer_update_op1_const(op_array, opline, val TSRMLS_CC);
			break;
		}
		
		if (ZEND_OP2_TYPE(opline) == IS_VAR &&
			ZEND_OP2(opline).var == var) {
			switch (opline->opcode) {
				case ZEND_ASSIGN_REF:
					return 0;
				default:
					break;
			}
			zend_optimizer_update_op2_const(op_array, opline, val TSRMLS_CC);
			break;
		}
		opline++;
	}

	return 1;
}

void zend_optimizer_replace_tmp_by_const(zend_op_array *op_array,
                                         zend_op       *opline,
                                         uint32_t      var,
                                         zval          *val
                                         TSRMLS_DC)
{
	zend_op *end = op_array->opcodes + op_array->last;

	while (opline < end) {
		if (ZEND_OP1_TYPE(opline) == IS_TMP_VAR &&
			ZEND_OP1(opline).var == var) {

			/* In most cases IS_TMP_VAR operand may be used only once.
			 * The operands are usually destroyed by the opcode handler.
			 * ZEND_CASE is an exception, that keeps operand unchanged,
			 * and allows its reuse. The number of ZEND_CASE instructions
			 * usually terminated by ZEND_FREE that finally kills the value.
			 */
			if (opline->opcode == ZEND_CASE) {
				zval old_val;
				ZVAL_COPY_VALUE(&old_val, val);
				zval_copy_ctor(val);
				zend_optimizer_update_op1_const(op_array, opline, val TSRMLS_CC);
				ZVAL_COPY_VALUE(val, &old_val);
			} else if (opline->opcode == ZEND_FREE) {
				MAKE_NOP(opline);
				break;
			} else {				
				zend_optimizer_update_op1_const(op_array, opline, val TSRMLS_CC);
				val = NULL;
				break;
			}
		}

		if (ZEND_OP2_TYPE(opline) == IS_TMP_VAR &&
			ZEND_OP2(opline).var == var) {

			zend_optimizer_update_op2_const(op_array, opline, val TSRMLS_CC);
			/* TMP_VAR may be used only once */
			val = NULL;
			break;
		}
		opline++;
	}
	if (val) {
		zval_dtor(val);
	}
}

static void zend_optimize(zend_op_array      *op_array,
                          zend_optimizer_ctx *ctx TSRMLS_DC)
{
	if (op_array->type == ZEND_EVAL_CODE) {
		return;
	}

	/* pass 1
	 * - substitute persistent constants (true, false, null, etc)
	 * - perform compile-time evaluation of constant binary and unary operations
	 * - optimize series of ADD_STRING and/or ADD_CHAR
	 * - convert CAST(IS_BOOL,x) into BOOL(x)
	 */
	if (ZEND_OPTIMIZER_PASS_1 & OPTIMIZATION_LEVEL) {
		zend_optimizer_pass1(op_array, ctx TSRMLS_CC);
	}

	/* pass 2:
	 * - convert non-numeric constants to numeric constants in numeric operators
	 * - optimize constant conditional JMPs
	 * - optimize static BRKs and CONTs
	 * - pre-evaluate constant function calls
	 */
	if (ZEND_OPTIMIZER_PASS_2 & OPTIMIZATION_LEVEL) {
		zend_optimizer_pass2(op_array TSRMLS_CC);
	}

	/* pass 3:
	 * - optimize $i = $i+expr to $i+=expr
	 * - optimize series of JMPs
	 * - change $i++ to ++$i where possible
	 */
	if (ZEND_OPTIMIZER_PASS_3 & OPTIMIZATION_LEVEL) {
		zend_optimizer_pass3(op_array TSRMLS_CC);
	}

	/* pass 4:
	 * - INIT_FCALL_BY_NAME -> DO_FCALL
	 */
	if (ZEND_OPTIMIZER_PASS_4 & OPTIMIZATION_LEVEL) {
		optimize_func_calls(op_array, ctx TSRMLS_CC);
	}

	/* pass 5:
	 * - CFG optimization
	 */
	if (ZEND_OPTIMIZER_PASS_5 & OPTIMIZATION_LEVEL) {
		optimize_cfg(op_array, ctx TSRMLS_CC);
	}

	/* pass 9:
	 * - Optimize temp variables usage
	 */
	if (ZEND_OPTIMIZER_PASS_9 & OPTIMIZATION_LEVEL) {
		optimize_temporary_variables(op_array, ctx);
	}

	/* pass 10:
	 * - remove NOPs
	 */
	if (((ZEND_OPTIMIZER_PASS_10|ZEND_OPTIMIZER_PASS_5) & OPTIMIZATION_LEVEL) == ZEND_OPTIMIZER_PASS_10) {
		zend_optimizer_nop_removal(op_array);
	}

	/* pass 11:
	 * - Compact literals table 
	 */
	if (ZEND_OPTIMIZER_PASS_11 & OPTIMIZATION_LEVEL) {
		zend_optimizer_compact_literals(op_array, ctx TSRMLS_CC);
	}
}

static void zend_accel_optimize(zend_op_array      *op_array,
                                zend_optimizer_ctx *ctx TSRMLS_DC)
{
	zend_op *opline, *end;

	/* Revert pass_two() */
	opline = op_array->opcodes;
	end = opline + op_array->last;
	while (opline < end) {
		if (opline->op1_type == IS_CONST) {
			opline->op1.constant = opline->op1.zv - op_array->literals;
		}
		if (opline->op2_type == IS_CONST) {
			opline->op2.constant = opline->op2.zv - op_array->literals;
		}
		switch (opline->opcode) {
			case ZEND_JMP:
			case ZEND_GOTO:
			case ZEND_FAST_CALL:
				ZEND_OP1(opline).opline_num = ZEND_OP1(opline).jmp_addr - op_array->opcodes;
				break;
			case ZEND_JMPZNZ:
				/* relative offset into absolute index */
				opline->extended_value = (zend_op*)(((char*)opline) + opline->extended_value) - op_array->opcodes;
				/* break omitted intentionally */
			case ZEND_JMPZ:
			case ZEND_JMPNZ:
			case ZEND_JMPZ_EX:
			case ZEND_JMPNZ_EX:
			case ZEND_JMP_SET:
			case ZEND_JMP_SET_VAR:
			case ZEND_NEW:
			case ZEND_FE_RESET:
			case ZEND_FE_FETCH:
				ZEND_OP2(opline).opline_num = ZEND_OP2(opline).jmp_addr - op_array->opcodes;
				break;
		}
		opline++;
	}

	/* Do actual optimizations */
	zend_optimize(op_array, ctx TSRMLS_CC);	
	
	/* Redo pass_two() */
	opline = op_array->opcodes;
	end = opline + op_array->last;
	while (opline < end) {
		if (opline->op1_type == IS_CONST) {
			opline->op1.zv = &op_array->literals[opline->op1.constant];
		}
		if (opline->op2_type == IS_CONST) {
			opline->op2.zv = &op_array->literals[opline->op2.constant];
		}
		switch (opline->opcode) {
			case ZEND_JMP:
			case ZEND_GOTO:
			case ZEND_FAST_CALL:
				ZEND_OP1(opline).jmp_addr = &op_array->opcodes[ZEND_OP1(opline).opline_num];
				break;
			case ZEND_JMPZNZ:
				/* absolute index to relative offset */
				opline->extended_value = (char*)(op_array->opcodes + opline->extended_value) - (char*)opline;
				/* break omitted intentionally */
			case ZEND_JMPZ:
			case ZEND_JMPNZ:
			case ZEND_JMPZ_EX:
			case ZEND_JMPNZ_EX:
			case ZEND_JMP_SET:
			case ZEND_JMP_SET_VAR:
			case ZEND_NEW:
			case ZEND_FE_RESET:
			case ZEND_FE_FETCH:
				ZEND_OP2(opline).jmp_addr = &op_array->opcodes[ZEND_OP2(opline).opline_num];
				break;
		}
		ZEND_VM_SET_OPCODE_HANDLER(opline);
		opline++;
	}
}

int zend_accel_script_optimize(zend_persistent_script *script TSRMLS_DC)
{
	uint idx, j;
	Bucket *p, *q;
	zend_class_entry *ce;
	zend_op_array *op_array;
	zend_optimizer_ctx ctx;

	ctx.arena = zend_arena_create(64 * 1024);
	ctx.script = script;
	ctx.constants = NULL;

	zend_accel_optimize(&script->main_op_array, &ctx TSRMLS_CC);

	for (idx = 0; idx < script->function_table.nNumUsed; idx++) {
		p = script->function_table.arData + idx;
		if (Z_TYPE(p->val) == IS_UNDEF) continue;
		op_array = (zend_op_array*)Z_PTR(p->val);
		zend_accel_optimize(op_array, &ctx TSRMLS_CC);
	}

	for (idx = 0; idx < script->class_table.nNumUsed; idx++) {
		p = script->class_table.arData + idx;
		if (Z_TYPE(p->val) == IS_UNDEF) continue;
		ce = (zend_class_entry*)Z_PTR(p->val);
		for (j = 0; j < ce->function_table.nNumUsed; j++) {
			q = ce->function_table.arData + j;
			if (Z_TYPE(q->val) == IS_UNDEF) continue;
			op_array = (zend_op_array*)Z_PTR(q->val);
			if (op_array->scope == ce) {
				zend_accel_optimize(op_array, &ctx TSRMLS_CC);
			} else if (op_array->type == ZEND_USER_FUNCTION) {
				zend_op_array *orig_op_array;
				if ((orig_op_array = zend_hash_find_ptr(&op_array->scope->function_table, q->key)) != NULL) {
					HashTable *ht = op_array->static_variables;
					*op_array = *orig_op_array;
					op_array->static_variables = ht;
				}
			}
		}
	}

	if (ctx.constants) {
		zend_hash_destroy(ctx.constants);
	}
	zend_arena_destroy(ctx.arena);

	return 1;
}
