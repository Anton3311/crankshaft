#include "x64.h"

#include "code_gen/backends/x64_reg_alloc.h"

inline uint8_t _bit_count_from_index(uint8_t i) {
	return (1 << i) * 8;
}

static uint8_t s_instr_value_bit_count[INSTR_COUNT] = {
	[INSTR_NO_OP]                  = 0,

	[INSTR_UNINITIALIZED_8]        = 8,
	[INSTR_UNINITIALIZED_16]       = 16,
	[INSTR_UNINITIALIZED_32]       = 32,
	[INSTR_UNINITIALIZED_64]       = 64,

	[INSTR_CONST_8]                = 8,
	[INSTR_CONST_16]               = 16,
	[INSTR_CONST_32]               = 32,
	[INSTR_CONST_64]               = 64,

	[INSTR_CONST_STRING]           = 64,

	[INSTR_BIN_OP_8]               = 8,
	[INSTR_BIN_OP_16]              = 16,
	[INSTR_BIN_OP_32]              = 32,
	[INSTR_BIN_OP_64]              = 64,

	[INSTR_BITWISE_NOT_8]          = 8,
	[INSTR_BITWISE_NOT_16]         = 8,
	[INSTR_BITWISE_NOT_32]         = 8,
	[INSTR_BITWISE_NOT_64]         = 8,

	[INSTR_COMPARE_8]              = 0,
	[INSTR_COMPARE_16]             = 0,
	[INSTR_COMPARE_32]             = 0,
	[INSTR_COMPARE_64]             = 0,

	[INSTR_BOOL_TO_INT]            = 8,

	[INSTR_NEGATE_8]               = 8,
	[INSTR_NEGATE_16]              = 16,
	[INSTR_NEGATE_32]              = 32,
	[INSTR_NEGATE_64]              = 64,

	[INSTR_UNSIGNED_EXTEND_TO_16]  = 16,
	[INSTR_UNSIGNED_EXTEND_TO_32]  = 32,
	[INSTR_UNSIGNED_EXTEND_TO_64]  = 64,

	[INSTR_SIGNED_EXTEND_TO_16]    = 16,
	[INSTR_SIGNED_EXTEND_TO_32]    = 32,
	[INSTR_SIGNED_EXTEND_TO_64]    = 64,

	[INSTR_REDUCE_TO_8]            = 8,
	[INSTR_REDUCE_TO_16]           = 16,
	[INSTR_REDUCE_TO_32]           = 32,

	[INSTR_PTR_LOAD_8]             = 8,
	[INSTR_PTR_LOAD_16]            = 16,
	[INSTR_PTR_LOAD_32]            = 32,
	[INSTR_PTR_LOAD_64]            = 64,

	[INSTR_PTR_STORE_8]            = 8,
	[INSTR_PTR_STORE_16]           = 16,
	[INSTR_PTR_STORE_32]           = 32,
	[INSTR_PTR_STORE_64]           = 64,

	[INSTR_LOAD_ARG_8]             = 8,
	[INSTR_LOAD_ARG_16]            = 16,
	[INSTR_LOAD_ARG_32]            = 32,
	[INSTR_LOAD_ARG_64]            = 64,
	[INSTR_LOAD_ARG_STACK]         = 0,

	[INSTR_STACK_ALLOC]            = 0,
	[INSTR_STACK_ADDR]             = 64,

	[INSTR_BRANCH]                 = 0,
	[INSTR_JUMP]                   = 0,

	[INSTR_RETURN_VALUE]           = 0,

	[INSTR_LOAD_FUNCTION_ADDR]     = 64,
	[INSTR_LOAD_EXTERNAL_FUNCTION_ADDR] = 64,

	[INSTR_CALL_INDIRECT]          = 64,
	[INSTR_CALL_DIRECT]            = 64,

	[INSTR_REGION]                 = 0,

	[INSTR_PHI]                    = 64,
	[INSTR_SELECT]                 = 0,
};

static X64Register CDECL_CALLER_SAVED[] = {
	X64_REG_A,
	X64_REG_C,
	X64_REG_D,
	X64_REG_8,
	X64_REG_9,
	X64_REG_10,
	X64_REG_11
};

static X64Register CDECL_CALLEE_SAVED[] = {
	X64_REG_B,
	X64_REG_BP,
	X64_REG_DI,
	X64_REG_SI,
	X64_REG_SP,
	X64_REG_12,
	X64_REG_13,
	X64_REG_14,
	X64_REG_15,
};

static X64Register CDECL_ARG_REGS[] = { X64_REG_C, X64_REG_D, X64_REG_8, X64_REG_9 };

static const char* X64_REG_BASE_NAMES[] = {
	"A",
	"C",
	"D",
	"B",
	"SP",
	"BP",
	"SI",
	"DI",
	"R8",
	"R9",
	"R10",
	"R11",
	"R12",
	"R13",
	"R14",
	"R15",
};

static void _format_reg_name(StringBuilder* builder, uint16_t reg_index, uint8_t reg_bit_count) {
	assert(reg_index < 16);

	char name_prefix = 0;
	char name_sufix = 0;

	switch (reg_bit_count) {
	case 8:
		name_sufix = 'L';
		break;
	case 16:
		if (reg_index < 4) {
			name_sufix = 'X';
		} else if (reg_index >= 8) {
			name_sufix = 'W';
		}
		break;
	case 32:
		if (reg_index < 4) {
			name_prefix = 'E';
			name_sufix = 'X';
		} else if (reg_index < 8) {
			name_prefix = 'E';
		} else {
			name_sufix = 'D';
		}
		
		break;
	case 64:
		if (reg_index < 4) {
			name_prefix = 'R';
			name_sufix = 'X';
		} else if (reg_index < 8) {
			name_prefix = 'R';
		}
		break;
	}

	if (name_prefix) {
		str_builder_append_char(builder, name_prefix);
	}

	str_builder_append_cstr(builder, X64_REG_BASE_NAMES[reg_index]);

	if (name_sufix) {
		str_builder_append_char(builder, name_sufix);
	}
}

//
// Code Generation
//

static uint16_t _collect_available_registers(X64CodeGenerator* gen, InstrIndex instr_index);

inline void _emit_load_const_64(CodeBuffer* buffer, X64Register reg, uint64_t value) {
	encode_2(buffer,
			MNEMONIC_MOV,
			operand_reg(reg, 64),
			operand_imm(value, 64));
}

inline void _emit_load_const_32(CodeBuffer* buffer, X64Register reg, uint32_t value) {
	encode_2(buffer,
			MNEMONIC_MOV,
			operand_reg(reg, 32),
			operand_imm(value, 32));
}

inline void _emit_load_const_16(CodeBuffer* buffer, X64Register reg, uint16_t value) {
	encode_2(buffer,
			MNEMONIC_MOV,
			operand_reg(reg, 16),
			operand_imm(value, 16));
}

inline void _emit_load_const_8(CodeBuffer* buffer, X64Register reg, uint8_t value) {
	encode_2(buffer,
			MNEMONIC_MOV,
			operand_reg(reg, 8),
			operand_imm(value, 8));
}

// NOTE: Not emitted when src and dst match. Don't use as a 32-bit movzx for that reason.
inline void _emit_mov_regs(CodeBuffer* buffer, X64Register src, X64Register dst, uint8_t reg_bit_count) {
	if (src == dst) {
		return;
	}

	encode_2(buffer,
			MNEMONIC_MOV,
			operand_reg(dst, reg_bit_count),
			operand_reg(src, reg_bit_count));
}

static void _emit_sub_rsp(CodeBuffer* buffer, uint32_t offset) {
	if (offset == 0) {
		return;
	}

	encode_2(buffer,
			MNEMONIC_SUB,
			operand_reg(X64_REG_SP, 64),
			operand_imm(offset, 32));
}

static void _emit_add_rsp(CodeBuffer* buffer, uint32_t offset) {
	if (offset == 0) {
		return;
	}

	encode_2(buffer,
			MNEMONIC_ADD,
			operand_reg(X64_REG_SP, 64),
			operand_imm(offset, 32));
}

static void _x64_generate_phi_copies(X64CodeGenerator* gen,
		uint16_t region_id,
		CodeBuffer* code_buffer) {

	profile_scope_start(__func__);

	Arena* temp_allocator = gen->allocator;
	ArenaRegion temp = arena_begin_temp(temp_allocator);

	const InstrIndexArray phi_variants = gen->phi_variants_per_region[region_id];
	const InstrIndex* phi_nodes = gen->phi_node_of_variant[region_id];

	size_t location_count = 0;
	InstrStorageLocation* input_locations = arena_alloc_array(temp_allocator,
			InstrStorageLocation,
			phi_variants.count);
	X64Register* target_locations = arena_alloc_array(temp_allocator,
			X64Register,
			phi_variants.count);
	
	uint16_t allowed_temp_registers = UINT16_MAX;

	for (uint16_t i = 0; i < phi_variants.count; i += 1) {
		InstrIndex variant_index = phi_variants.instr[i];
		InstrIndex phi_node_index = phi_nodes[i];

		const Instr* value = &gen->instr_buffer.instr[variant_index.value];
		const Instr* phi_node = &gen->instr_buffer.instr[phi_node_index.value];
		assert(phi_node->kind == INSTR_PHI);

		const InstrStorageLocation value_storage = gen->instr_storage[phi_variants.instr[i].value];
		const InstrStorageLocation phi_storage = gen->instr_storage[phi_node_index.value];

		if (phi_storage.kind == INSTR_STORAGE_NONE) {
			// Phi node was never allocated. It means it is also never going to be used.
			continue;
		}

		assert(value_storage.kind == INSTR_STORAGE_REG);
		assert(phi_storage.kind == INSTR_STORAGE_REG);

		if (value_storage.reg == phi_storage.reg) {
			continue;
		}

		allowed_temp_registers &= _collect_available_registers(gen, phi_node_index);
		// allowed_temp_registers &= ~(1 << value_storage.reg);

		input_locations[location_count] = value_storage;
		target_locations[location_count] = phi_storage.reg;
		location_count += 1;
	}

	RegisterMoveArray moves = _parallel_move_values(input_locations,
			target_locations,
			location_count,
			allowed_temp_registers,
			gen->allocator,
			gen->temp_allocator);

	for (size_t i = 0; i < moves.count; i += 1) {
		RegisterMove move = moves.moves[i];
		_emit_mov_regs(code_buffer, move.src, move.dst, 64);
	}

	arena_end_temp(temp);

	profile_scope_end();
}

static void _emit_mul(CodeBuffer* buffer,
		MnemonicKind mnemonic,
		X64Register left_reg,
		X64Register right_reg,
		X64Register dst_reg,
		uint8_t bit_count) {
	// FIXME: Make sure that nothing else is using `AH` at this moment, since it will be
	//        overriden by the `imul` result.
	X64Register mul_output_reg = X64_REG_A;
	bool should_save_output = dst_reg != mul_output_reg;

	if (should_save_output) {
		encode_1(buffer, MNEMONIC_PUSH, operand_reg(mul_output_reg, 64));
	}

	if (left_reg == mul_output_reg) {
		encode_1(buffer, mnemonic, operand_reg(right_reg, bit_count));
	} else if (right_reg == mul_output_reg) {
		encode_1(buffer, mnemonic, operand_reg(left_reg, bit_count));
	} else {
		_emit_mov_regs(buffer, left_reg, mul_output_reg, bit_count);
		encode_1(buffer, mnemonic, operand_reg(right_reg, bit_count));
	}

	_emit_mov_regs(buffer, X64_REG_A, dst_reg, bit_count);

	if (should_save_output) {
		encode_1(buffer, MNEMONIC_POP, operand_reg(mul_output_reg, 64));
	}
}

static void _emit_div_mod(CodeBuffer* buffer,
		MnemonicKind mnemonic,
		X64Register left_reg,
		X64Register right_reg,
		X64Register dst_reg,
		bool select_quotient,
		uint8_t bit_count) {
	assert(mnemonic == MNEMONIC_DIV || mnemonic == MNEMONIC_IDIV);

	X64Register quotient_output_reg = X64_REG_A;
	X64Register remainder_output_reg = X64_REG_D;
	bool should_save_a = dst_reg != quotient_output_reg;
	bool should_save_d = dst_reg != remainder_output_reg;

	bool use_temp_r8 = left_reg == X64_REG_A || left_reg == X64_REG_D;
	bool use_temp_r9 = right_reg == X64_REG_A || right_reg == X64_REG_D;

	if (should_save_a) {
		encode_1(buffer, MNEMONIC_PUSH, operand_reg(quotient_output_reg, 64));
	}

	if (should_save_d) {
		encode_1(buffer, MNEMONIC_PUSH, operand_reg(remainder_output_reg, 64));
	}

	if (use_temp_r8) {
		encode_1(buffer, MNEMONIC_PUSH, operand_reg(X64_REG_8, 64));
		encode_2(buffer,
				MNEMONIC_MOV,
				operand_reg(X64_REG_8, bit_count),
				operand_reg(left_reg, bit_count));

		left_reg = X64_REG_8;
	}

	if (use_temp_r9) {
		encode_1(buffer, MNEMONIC_PUSH, operand_reg(X64_REG_9, 64));
		encode_2(buffer,
				MNEMONIC_MOV,
				operand_reg(X64_REG_9, bit_count),
				operand_reg(right_reg, bit_count));

		right_reg = X64_REG_9;
	}

	if (mnemonic == MNEMONIC_IDIV) {
		switch (bit_count) {
		case 8:
			encode_2(buffer, MNEMONIC_MOVSX, operand_reg(X64_REG_A, 16), operand_reg(left_reg, 8));
			break;
		case 16:
			_emit_mov_regs(buffer, left_reg, X64_REG_A, bit_count);
			encode_n(buffer, MNEMONIC_CWD, NULL, 0);
			break;
		case 32:
			_emit_mov_regs(buffer, left_reg, X64_REG_A, bit_count);
			encode_n(buffer, MNEMONIC_CDQ, NULL, 0);
			break;
		case 64:
			_emit_mov_regs(buffer, left_reg, X64_REG_A, bit_count);
			encode_n(buffer, MNEMONIC_CQO, NULL, 0);
			break;
		default:
			unreachable();
		}
	} else {
		switch (bit_count) {
		case 8:
			encode_2(buffer, MNEMONIC_MOVZX, operand_reg(X64_REG_A, 16), operand_reg(left_reg, 8));
			break;
		case 16:
			_emit_load_const_16(buffer, X64_REG_D, 0);
			_emit_mov_regs(buffer, left_reg, X64_REG_A, bit_count);
			break;
		case 32:
			_emit_load_const_32(buffer, X64_REG_D, 0);
			_emit_mov_regs(buffer, left_reg, X64_REG_A, bit_count);
			break;
		case 64:
			_emit_load_const_64(buffer, X64_REG_D, 0);
			_emit_mov_regs(buffer, left_reg, X64_REG_A, bit_count);
			break;
		default:
			unreachable();
		}
	}

	encode_1(buffer, mnemonic, operand_reg(right_reg, bit_count));

	if (bit_count == 8) {
		if (!select_quotient) {
			_emit_mov_regs(buffer, 4 /* AH */, X64_REG_A, 8);
		}

		_emit_mov_regs(buffer,
				select_quotient ? quotient_output_reg : X64_REG_A,
				dst_reg,
				bit_count);
	} else {
		_emit_mov_regs(buffer,
				select_quotient ? quotient_output_reg : remainder_output_reg,
				dst_reg,
				bit_count);
	}

	if (use_temp_r9) {
		encode_1(buffer, MNEMONIC_POP, operand_reg(X64_REG_9, 64));
	}

	if (use_temp_r8) {
		encode_1(buffer, MNEMONIC_POP, operand_reg(X64_REG_8, 64));
	}

	if (should_save_d) {
		encode_1(buffer, MNEMONIC_POP, operand_reg(remainder_output_reg, 64));
	}

	if (should_save_a) {
		encode_1(buffer, MNEMONIC_POP, operand_reg(quotient_output_reg, 64));
	}
}

void _emit_bitwise_shift(CodeBuffer* buffer,
		MnemonicKind mnemonic,
		X64Register value_reg,
		X64Register count_reg,
		X64Register dst_reg,
		uint8_t bit_count,
		uint16_t allowed_temp_registers,
		Arena* allocator,
		Arena* temp_allocator) {

	profile_scope_start(__func__);

	assert_msg(allowed_temp_registers != 0, "todo: Find a victim and spill");
	assert_msg(!has_flag(allowed_temp_registers, 1 << X64_REG_C),
				"`rcx` is a fixed of the shift instruction, thus can be used a temp register");

	if (dst_reg == X64_REG_C) {
		X64Register temp_dst = count_trailing_zeros(allowed_temp_registers);

		X64Register expected_locs[] = { temp_dst, X64_REG_C };
		InstrStorageLocation input_locs[2] = {
			(InstrStorageLocation) {
				.kind = INSTR_STORAGE_REG,
				.reg = value_reg,
			},
			(InstrStorageLocation) {
				.kind = INSTR_STORAGE_REG,
				.reg = count_reg,
			},
		};

		RegisterMoveArray moves = _parallel_move_values(input_locs,
				expected_locs,
				2, 0,
				allocator,
				temp_allocator);

		for (size_t i = 0; i < moves.count; i += 1) {
			RegisterMove move = moves.moves[i];
			_emit_mov_regs(buffer, move.src, move.dst, 64);
		}

		encode_1(buffer, mnemonic, operand_reg(temp_dst, bit_count));

		encode_2(buffer,
				MNEMONIC_MOV,
				operand_reg(dst_reg, bit_count),
				operand_reg(temp_dst, bit_count));
	} else {
		X64Register expected_locs[] = { dst_reg, X64_REG_C };
		InstrStorageLocation input_locs[2] = {
			(InstrStorageLocation) {
				.kind = INSTR_STORAGE_REG,
				.reg = value_reg,
			},
			(InstrStorageLocation) {
				.kind = INSTR_STORAGE_REG,
				.reg = count_reg,
			},
		};

		bool should_save_rcx = count_reg != X64_REG_C && dst_reg != X64_REG_C;

		if (should_save_rcx) {
			encode_1(buffer, MNEMONIC_PUSH, operand_reg(X64_REG_C, 64));
		}

		RegisterMoveArray moves = _parallel_move_values(input_locs,
				expected_locs,
				2, allowed_temp_registers,
				allocator,
				temp_allocator);

		for (size_t i = 0; i < moves.count; i += 1) {
			RegisterMove move = moves.moves[i];
			_emit_mov_regs(buffer, move.src, move.dst, 64);
		}

		encode_1(buffer, mnemonic, operand_reg(dst_reg, bit_count));

		if (should_save_rcx) {
			encode_1(buffer, MNEMONIC_POP, operand_reg(X64_REG_C, 64));
		}
	}

	profile_scope_end();
}

static void _emit_callee_prologue(X64CodeGenerator* gen, CodeBuffer* buffer) {
	for (size_t i = 0; i < array_size(CDECL_CALLEE_SAVED); i += 1) {
		encode_1(buffer, MNEMONIC_PUSH, operand_reg(CDECL_CALLEE_SAVED[i], 64));
	}

	_emit_sub_rsp(buffer, gen->stack_usage + 8);
}

static void _emit_callee_epilogue(X64CodeGenerator* gen, CodeBuffer* buffer) {
	_emit_add_rsp(buffer, gen->stack_usage + 8);

	for (size_t i = array_size(CDECL_CALLEE_SAVED); i > 0; i -= 1) {
		encode_1(buffer, MNEMONIC_POP, operand_reg(CDECL_CALLEE_SAVED[i - 1], 64));
	}
}

static uint16_t _collect_available_registers(X64CodeGenerator* gen, InstrIndex instr_index) {
	ArenaRegion temp = arena_begin_temp(gen->temp_allocator);
	InstrStorageLocation instr_storage = gen->instr_storage[instr_index.value];

	uint16_t allowed_temp_registers = UINT16_MAX;
	allowed_temp_registers &= ~(1 << instr_storage.reg);

	allowed_temp_registers &= ~(1 << X64_REG_SP);
	allowed_temp_registers &= ~(1 << X64_REG_BP);

	// HACK: Some times the register allocator might allocate the whole register
	//       to some instruction and also it's high part to the other, thus any
	//       writes by any of the two instructions will be reflected in two places.
	allowed_temp_registers &= ~(1 << X64_REG_SI);
	allowed_temp_registers &= ~(1 << X64_REG_DI);

	const Instr* instr = &gen->instr_buffer.instr[instr_index.value];
	if (instr->kind == INSTR_PHI) {
		InstrInputs inputs = instr->phi.variants;
		const InstrIndex* select_indices = &gen->instr_buffer.inputs_buffer[inputs.start];

		for (uint16_t i = 0; i < inputs.count; i += 1) {
			const Instr* select = &gen->instr_buffer.instr[select_indices[i].value];

			InstrStorageLocation loc = gen->instr_storage[select->select.value.value];
			if (loc.kind != INSTR_STORAGE_REG) {
				continue;
			}

			allowed_temp_registers &= ~(1 << loc.reg);
		}
	} else {
		InstrQueue queue;
		instr_queue_alloc(&queue, gen->temp_allocator, gen->instr_buffer.count);

		instr_enumerate_uses(&gen->instr_buffer, instr_index, &queue);

		for (size_t i = 0; i < queue.count; i += 1) {
			InstrStorageLocation loc = gen->instr_storage[queue.buffer[i].value];

			if (loc.kind != INSTR_STORAGE_REG) {
				continue;
			}

			allowed_temp_registers &= ~(1 << loc.reg);
		}
	}

	if (gen->function_signature.returns != NULL) {
		AbiParam* returns = gen->function_signature.returns;
		if (returns->kind == ABI_PARAM_STRUCT) {
			allowed_temp_registers &= ~(1 << CDECL_ARG_REGS[0]);
		}
	}

	InstrIndexArray edges = gen->interference_graph[instr_index.value];
	for (size_t j = 0; j < edges.count; j += 1) {
		InstrIndex interfering_instr = edges.instr[j];
		InstrStorageLocation loc = gen->instr_storage[interfering_instr.value];

		if (loc.kind != INSTR_STORAGE_REG) {
			continue;
		}

		allowed_temp_registers &= ~(1 << loc.reg);
	}

	arena_end_temp(temp);
	return allowed_temp_registers;
}

static void _emit_mem_copy_fixed(CodeBuffer* buffer,
		Operand src_operand,
		Operand dst_operand,
		uint16_t size,
		uint16_t temp_registers) {

	profile_scope_start(__func__);

	assert(src_operand.kind == OP_MEM);
	assert(dst_operand.kind == OP_MEM);

	if (size == 0) {
		profile_scope_end();
		return;
	}

	X64Register spill_victim = -1;
	X64Register temp_register = -1;
	if (temp_registers != 0) {
		temp_register = count_trailing_zeros(temp_registers);
	} else {
		uint16_t allowed_registers = UINT16_MAX;
		allowed_registers &= ~(1 << X64_REG_SP);
		allowed_registers &= ~(1 << X64_REG_BP);
		allowed_registers &= ~(1 << X64_REG_SI);
		allowed_registers &= ~(1 << X64_REG_DI);

		allowed_registers &= ~(1 << src_operand.mem.base_reg);
		allowed_registers &= ~(1 << dst_operand.mem.base_reg);

		spill_victim = count_trailing_zeros(allowed_registers);
		temp_register = spill_victim;
	}

	// NOTE: Spill the vicitim register on the stack, without moving the stack pointer. The spilled
	//       register is stored outside of the call frame at `rsp - 0x8`
	if (spill_victim != -1) {
		encode_2(buffer, MNEMONIC_MOV, operand_stack_mem(-8, 64), operand_reg(spill_victim, 64));
	}

	uint16_t sizes[] = { 8, 4, 2, 1 };
	uint16_t bit_counts[] = { 64, 32, 16, 8 };

	size_t bytes_left = size;
	size_t offset = 0;
	for (uint16_t size_index = 0; size_index < array_size(sizes); size_index += 1) {
		while (bytes_left >= sizes[size_index]) {
			Operand sized_src = src_operand;
			sized_src.bit_count = bit_counts[size_index]; 

			Operand sized_dst = dst_operand;
			sized_dst.bit_count = bit_counts[size_index]; 

			encode_2(buffer,
					MNEMONIC_MOV,
					operand_reg(temp_register, bit_counts[size_index]),
					sized_src);

			encode_2(buffer,
					MNEMONIC_MOV,
					sized_dst,
					operand_reg(temp_register, bit_counts[size_index]));

			src_operand.mem.disp += sizes[size_index];
			dst_operand.mem.disp += sizes[size_index];

			bytes_left -= sizes[size_index];
		}
	}

	if (spill_victim != -1) {
		encode_2(buffer, MNEMONIC_MOV, operand_reg(spill_victim, 64), operand_stack_mem(-8, 64));
	}

	profile_scope_end();
}

static void _emit_mem_zero_fixed(CodeBuffer* buffer,
		Operand dst_operand,
		uint16_t size,
		uint16_t temp_registers) {
	profile_scope_start(__func__);

	X64Register temp_register = count_trailing_zeros(temp_registers);
	_emit_load_const_64(buffer, temp_register, 0);

	uint16_t sizes[] = { 8, 4, 2, 1 };
	uint16_t bit_counts[] = { 64, 32, 16, 8 };

	size_t bytes_left = size;
	size_t offset = 0;
	for (uint16_t size_index = 0; size_index < array_size(sizes); size_index += 1) {
		while (bytes_left >= sizes[size_index]) {
			Operand sized_dst = dst_operand;
			sized_dst.bit_count = bit_counts[size_index]; 

			encode_2(buffer,
					MNEMONIC_MOV,
					sized_dst,
					operand_reg(temp_register, bit_counts[size_index]));

			dst_operand.mem.disp += sizes[size_index];

			bytes_left -= sizes[size_index];
		}
	}

	profile_scope_end();
}

typedef struct {
	bool is_direct;
	InstrInputs args;
	uint16_t callee_signature_index;
	
	union {
		struct {
			uint16_t callee_index;
		} direct;
		struct {
			InstrIndex address_instr;
		} indirect;
	};
} CallLoweringOptions;

const X64Register DEFAULT_CALLEE_ADDRESS_REGISTER = X64_REG_A;

static void _lower_call(X64CodeGenerator* gen,
		InstrIndex instr_index,
		uint16_t region_id,
		CodeBuffer* buffer,
		CallLoweringOptions options) {
	profile_scope_start(__func__);

	Arena* temp_allocator = gen->allocator;
	ArenaRegion temp = arena_begin_temp(temp_allocator);

	const uint32_t SHADOW_SPACE_SIZE = 32;

	const InstrBuffer* instr_buffer = &gen->instr_buffer;
	const InstrStorageLocation instr_storage = gen->instr_storage[instr_index.value];

	AbiSignature callee_signature = gen->function_call_signatures[options.callee_signature_index];

	if (callee_signature.returns) {
		if (callee_signature.returns->kind == ABI_PARAM_STRUCT
				&& callee_signature.returns->struct_size <= 8) {
			// Small structs that fit into a register, get returned through a register
			assert(instr_storage.kind == INSTR_STORAGE_REG);
		} else if (callee_signature.returns->kind == ABI_PARAM_STRUCT) {
			assert(instr_storage.kind == INSTR_STORAGE_STACK);
			assert(callee_signature.returns->struct_size > 8);
		} else if (callee_signature.returns->kind == ABI_PARAM_NORMAL) {
			assert(instr_storage.kind == INSTR_STORAGE_REG);
		} else {
			unreachable();
		}
	}

	// Push saved registers
	for (size_t i = 0; i < array_size(CDECL_CALLER_SAVED); i += 1) {
		encode_1(buffer,
				MNEMONIC_PUSH,
				operand_reg(CDECL_CALLER_SAVED[i], 64));
	}

	// NOTE: Since we've already pushed `CDECL_CALLEE_SAVED` registers, the stack pointer has moved,
	//       and we need to account for that.

	uint32_t stack_adjustment = array_size(CDECL_CALLER_SAVED) * 8;

	CallFrameLayout frame_layout = compute_call_frame_layout(
			&callee_signature,
			temp_allocator);

	_emit_sub_rsp(buffer, align(frame_layout.stack_slot_count * 8, 16));
	stack_adjustment += align(frame_layout.stack_slot_count * 8, 16);

	uint32_t call_frame_offset = frame_layout.stack_slot_count * 8;

	// The first major step here is to prepare the arguments.
	//
	// Arguments could be stored in different ways:
	// * directly in the register
	// * on the stack
	//
	// Thus the preparation has to be approached differently, based on how the argument value is
	// stored, while also taking into that the function might return an overrized struct, in that
	// case we also need to push the address of the return area, as the first argument.
	//
	// The approaches are defined as follows:
	// 1. Arguments stored directly in register, come first. They get moved into their corresponding
	//    locations (defined by the ABI) using parallel moves (see `_parallel_move_values`).
	//
	//    It is important to resolve this first, because further preparation steps will override
	//    some of the registers, potentially discarding the value of an argument.
	//
	// 2. Load the addresses of struct arguments (those that don't fit into a single register).
	//    This step is straightforward, it is a single `lea` instruction per argument.
	//
	// 3. Finally load the return area address, using the same `lea` instruction.

	uint16_t input_location_count = 0;
	uint16_t target_location_count = 0;

	uint16_t max_location_count = options.args.count;
	if (!options.is_direct) {
		// We also push the function address as one of the inputs.
		max_location_count += 1;
	}

	InstrStorageLocation* input_locations = arena_alloc_array(temp_allocator,
			InstrStorageLocation,
			max_location_count);
	X64Register* target_locations = arena_alloc_array(temp_allocator,
			X64Register,
			max_location_count);

	{
		uint16_t arg_index = 0;
		for (size_t i = 0; i < callee_signature.param_count; i += 1) {
			AbiParam param = callee_signature.params[i];

			if (param.kind == ABI_PARAM_NORMAL || param.kind == ABI_PARAM_STRUCT) {
				InstrIndex arg_instr = instr_buffer->inputs_buffer[options.args.start + arg_index];
				InstrStorageLocation arg_location = gen->instr_storage[arg_instr.value];
				InstrStorageLocation target_location = frame_layout.locations[i];

				if (arg_location.kind == INSTR_STORAGE_REG
						&& target_location.kind == INSTR_STORAGE_REG) {

					assert(input_location_count < max_location_count);
					input_locations[input_location_count] = arg_location;
					input_location_count += 1;

					assert(target_location_count < max_location_count);
					target_locations[target_location_count] = frame_layout.locations[i].reg;
					target_location_count += 1;
				} else if (arg_location.kind == INSTR_STORAGE_REG
						&& target_location.kind == INSTR_STORAGE_CALL_FRAME) {

					uint32_t slot_offset = target_location.call_frame.slot * 8;
					encode_2(buffer,
							MNEMONIC_MOV,
							operand_stack_mem((int32_t)(slot_offset), 64),
							operand_reg(arg_location.reg, 64));
				}

				arg_index += 1;
			} else if (param.kind == ABI_PARAM_RETURN_LOCATION) {
 				// `ABI_PARAM_RETURN_LOCATION` doesn't correspond to any of the arguments, so simply
				// skip it without moving to the next argument.
			}
		}
	}

	// In case it is an indirect call, add the instruciton that computes the callee address as one
	// of the inputs.
	if (!options.is_direct) {
		assert(input_location_count < max_location_count);
		InstrIndex addr_instr = options.indirect.address_instr;
		input_locations[input_location_count] = gen->instr_storage[addr_instr.value];
		input_location_count += 1;

		assert(target_location_count < max_location_count);
		target_locations[target_location_count] = DEFAULT_CALLEE_ADDRESS_REGISTER;
		target_location_count += 1;
	}

	assert(input_location_count == target_location_count);

	uint16_t allowed_temp_registers = _collect_available_registers(gen, instr_index);
	for (size_t i = 0; i < target_location_count; i += 1) {
		allowed_temp_registers &= ~(1 << target_locations[i]);
	}

	RegisterMoveArray parallel_moves = _parallel_move_values(
			input_locations,
			target_locations,
			input_location_count,
			allowed_temp_registers,
			gen->allocator,
			gen->temp_allocator);

	for (size_t i = 0; i < parallel_moves.count; i += 1) {
		RegisterMove move = parallel_moves.moves[i];
		_emit_mov_regs(buffer, move.src, move.dst, 64);
	}

	// Now move the struct argument addreses into their corresponding registers

	{
		uint16_t arg_index = 0;
		for (size_t i = 0; i < callee_signature.param_count; i += 1) {
			AbiParam param = callee_signature.params[i];

			if (param.kind == ABI_PARAM_NORMAL) {
				arg_index += 1;
			} else if (param.kind == ABI_PARAM_STRUCT) {
				InstrIndex arg_instr = instr_buffer->inputs_buffer[options.args.start + arg_index];
				InstrStorageLocation arg_location = gen->instr_storage[arg_instr.value];
				InstrStorageLocation target_location = frame_layout.locations[i];

				if (arg_location.kind == INSTR_STORAGE_STACK &&
						target_location.kind == INSTR_STORAGE_REG) {

					int32_t arg_offset = (int32_t)(arg_location.stack.offset + stack_adjustment);

					encode_2(buffer,
							MNEMONIC_LEA,
							operand_reg(target_location.reg, 64),
							operand_stack_mem(arg_offset, 64));
				} else if (arg_location.kind == INSTR_STORAGE_STACK &&
						target_location.kind == INSTR_STORAGE_CALL_FRAME) {

					assert(allowed_temp_registers != 0);

					// This will be a memory to memory move, so we need a temporary register to
					// resolve it.
					X64Register temp_register = count_trailing_zeros(allowed_temp_registers);

					int32_t arg_offset = (int32_t)(arg_location.stack.offset + stack_adjustment);

					encode_2(buffer,
							MNEMONIC_LEA,
							operand_reg(temp_register, 64),
							operand_stack_mem(arg_offset, 64));
					
					uint32_t slot_offset = target_location.call_frame.slot * 8;
					encode_2(buffer,
							MNEMONIC_MOV,
							operand_stack_mem((int32_t)(slot_offset), 64),
							operand_reg(temp_register, 64));
				}

				arg_index += 1;
			} else if (param.kind == ABI_PARAM_RETURN_LOCATION) {
				// Load the address of the stack location that recieves the returned struct
				size_t return_value_stack_offset =
					instr_storage.stack.offset + stack_adjustment;

				InstrStorageLocation target_location = frame_layout.locations[i];
				assert(target_location.kind == INSTR_STORAGE_REG);

				encode_2(buffer,
						MNEMONIC_LEA,
						operand_reg(target_location.reg, 64),
						operand_stack_mem((int32_t)return_value_stack_offset, 64));
			}
		}
	}

	// push shadow space
	_emit_sub_rsp(buffer, SHADOW_SPACE_SIZE);

	// Do the call
	if (instr_buffer->instr[instr_index.value].kind == INSTR_CALL_DIRECT) {
		encode_1(buffer, MNEMONIC_CALL, operand_rel32(0));

		assert(gen->call_addr_placeholder_count < gen->call_addr_placeholder_capacity);
		size_t index = gen->call_addr_placeholder_count;
		gen->call_addr_placeholder_count += 1;

		CallAddressPlaceholder* addr_placeholder = &gen->call_addr_placeholders[index];
		gen->call_addr_placeholder_regions[index] = region_id;

		addr_placeholder->instruction_end_offset = buffer->size;
		addr_placeholder->addr_offset = buffer->size - 4;
		addr_placeholder->function_index = options.direct.callee_index;
		addr_placeholder->kind = CALL_ADDR_RELATIVE;
	} else {
		encode_1(buffer, MNEMONIC_CALL, operand_reg(DEFAULT_CALLEE_ADDRESS_REGISTER, 64));
	}

	// pop shadow space
	_emit_add_rsp(buffer, SHADOW_SPACE_SIZE);

	// pop arguments passed through the stack
	_emit_add_rsp(buffer, align(frame_layout.stack_slot_count * 8, 16));

	X64Register return_register = X64_REG_COUNT;
	if (callee_signature.returns != NULL && instr_storage.kind == INSTR_STORAGE_REG) {
		return_register = instr_storage.reg;

		// Now move the return value into a the proper register dedicated exactly for the return
		// value of this call instruction
		_emit_mov_regs(buffer, X64_REG_A, instr_storage.reg, 64);
	}

	// Pop saved registers in reverse order
	for (size_t i = array_size(CDECL_CALLER_SAVED); i > 0; i -= 1) {
		X64Register reg = CDECL_CALLER_SAVED[i - 1];
		
		if (reg == return_register) {
			// This register contains the return value, so don't restore it in order not to override
			// the return value.
			_emit_add_rsp(buffer, 8);
		} else {
			encode_1(buffer,
					MNEMONIC_POP,
					operand_reg(reg, 64));
		}
	}

	arena_end_temp(temp);
	profile_scope_end();
}

static void _lower_instr(X64CodeGenerator* gen,
		InstrIndex instr_index,
		uint16_t region_id,
		CodeBuffer* buffer) {

	assert(instr_index.value < gen->instr_buffer.count);

	const InstrBuffer* instr_buffer = &gen->instr_buffer;
	const Instr* instr = &gen->instr_buffer.instr[instr_index.value];
	const InstrStorageLocation instr_storage = gen->instr_storage[instr_index.value];

	switch (instr->kind) {
	case INSTR_NO_OP:
		return;
	
	case INSTR_UNINITIALIZED_8:
	case INSTR_UNINITIALIZED_16:
	case INSTR_UNINITIALIZED_32:
	case INSTR_UNINITIALIZED_64:
		return;

	case INSTR_CONST_8:
		assert(instr_storage.kind == INSTR_STORAGE_REG);
		_emit_load_const_8(buffer, instr_storage.reg, instr->const_8.u);
		return;
	case INSTR_CONST_16:
		assert(instr_storage.kind == INSTR_STORAGE_REG);
		_emit_load_const_16(buffer, instr_storage.reg, instr->const_16.u);
		return;
	case INSTR_CONST_32:
		assert(instr_storage.kind == INSTR_STORAGE_REG);
		_emit_load_const_32(buffer, instr_storage.reg, instr->const_32.u);
		return;

	case INSTR_CONST_64:
		assert(instr_storage.kind == INSTR_STORAGE_REG);
		_emit_load_const_64(buffer, instr_storage.reg, instr->const_64.u);
		return;

	case INSTR_CONST_STRING: {
		assert(instr_storage.kind == INSTR_STORAGE_REG);

		uint32_t str_id = instr->const_string.string_id;
		const char* string = gen->merged_strings_buffer + gen->string_offsets[str_id];
		_emit_load_const_64(buffer, instr_storage.reg, (uint64_t)string);
		return;
	}

	case INSTR_BIN_OP_8:
	case INSTR_BIN_OP_16:
	case INSTR_BIN_OP_32:
	case INSTR_BIN_OP_64: {
		uint8_t bit_count = _bit_count_from_index(instr->kind - INSTR_BIN_OP_8);

		const InstrStorageLocation dst_loc = gen->instr_storage[instr_index.value];
		const InstrStorageLocation left_loc = gen->instr_storage[instr->bin_op.left.value];
		const InstrStorageLocation right_loc = gen->instr_storage[instr->bin_op.right.value];
		assert(dst_loc.kind == INSTR_STORAGE_REG);
		assert(left_loc.kind == INSTR_STORAGE_REG);
		assert(right_loc.kind == INSTR_STORAGE_REG);

		uint8_t left_reg;
		uint8_t right_reg;

		// NOTE: We have two input register and one output.
		//       It might happen that the output register overlaps with one of the input registers.
		//       When encoding the instruction the register on the left is the output one, this is
		//       fine if the output register of this bin op overlaps with left input. However the
		//       other might happen, and the output might overlap with the right input, in such case
		//       if the operation is commutative we can just swap the inputs.
		//       
		//       For non-commutative ops the solution for is:
		//       If output overlaps with the left input, encode as is, this case doesn't require
		//       any special treatment.
		//       
		//       Otherwise if the output overlaps with right right input, push the left register
		//       onto the stack, do the computation while storing the result in the left input
		//       register, move the result to the right register, and finally restore the right
		//       register.
		if (instr_bin_op_is_commutative(instr->bin_op.kind)) {
			if (right_loc.reg == dst_loc.reg) {
				_emit_mov_regs(buffer, right_loc.reg, dst_loc.reg, bit_count);

				left_reg = dst_loc.reg;
				right_reg = left_loc.reg;
			} else {
				_emit_mov_regs(buffer, left_loc.reg, dst_loc.reg, bit_count);

				left_reg = dst_loc.reg;
				right_reg = right_loc.reg;
			}
		} else {
			left_reg = left_loc.reg;
			right_reg = right_loc.reg;
		}

		switch (instr->bin_op.kind) {
		case INSTR_BIN_ADD:
			encode_2(buffer,
					MNEMONIC_ADD,
					operand_reg(left_reg, bit_count),
					operand_reg(right_reg, bit_count));
			return;
		case INSTR_BIN_SUB: {
			if (dst_loc.reg == right_loc.reg) {
				// NOTE: When saving the register, push/pop the whole 64-bit register
				encode_1(buffer,
						MNEMONIC_PUSH,
						operand_reg(left_loc.reg, 64));

				encode_2(buffer,
						MNEMONIC_SUB,
						operand_reg(left_reg, bit_count),
						operand_reg(right_reg, bit_count));

				_emit_mov_regs(buffer, left_loc.reg, right_loc.reg, bit_count);

				encode_1(buffer,
						MNEMONIC_POP,
						operand_reg(left_loc.reg, 64));
			} else if (dst_loc.reg == left_loc.reg) {
				encode_2(buffer,
						MNEMONIC_SUB,
						operand_reg(left_reg, bit_count),
						operand_reg(right_reg, bit_count));
			} else {
				encode_2(buffer,
						MNEMONIC_MOV,
						operand_reg(dst_loc.reg, bit_count),
						operand_reg(left_reg, bit_count));

				encode_2(buffer,
						MNEMONIC_SUB,
						operand_reg(dst_loc.reg, bit_count),
						operand_reg(right_reg, bit_count));
			}

			return;
		}
		case INSTR_BIN_IMUL:
			if (bit_count == 8) {
				_emit_mul(buffer, MNEMONIC_IMUL, left_reg, right_reg, dst_loc.reg, 8);
			} else {
				assert(bit_count == 16 || bit_count == 32 || bit_count == 64);

				encode_2(buffer,
						MNEMONIC_IMUL,
						operand_reg(left_reg, bit_count),
						operand_reg(right_reg, bit_count));
			}
			return;
		case INSTR_BIN_UMUL:
			if (dst_loc.reg != X64_REG_D) {
				encode_1(buffer, MNEMONIC_PUSH, operand_reg(X64_REG_D, 64));
			}

			_emit_mul(buffer, MNEMONIC_MUL, left_reg, right_reg, dst_loc.reg, bit_count);

			if (dst_loc.reg != X64_REG_D) {
				encode_1(buffer, MNEMONIC_POP, operand_reg(X64_REG_D, 64));
			}
			return;
		case INSTR_BIN_IDIV:
			_emit_div_mod(buffer, MNEMONIC_IDIV, left_reg, right_reg, dst_loc.reg, true, bit_count);
			return;
		case INSTR_BIN_UDIV:
			_emit_div_mod(buffer, MNEMONIC_DIV, left_reg, right_reg, dst_loc.reg, true, bit_count);
			return;
		case INSTR_BIN_IMOD:
			_emit_div_mod(buffer,
					MNEMONIC_IDIV,
					left_reg,
					right_reg,
					dst_loc.reg,
					false,
					bit_count);
			return;
		case INSTR_BIN_UMOD:
			_emit_div_mod(buffer,
					MNEMONIC_DIV,
					left_reg,
					right_reg,
					dst_loc.reg,
					false,
					bit_count);
			return;
		case INSTR_BIN_AND:
			encode_2(buffer,
					MNEMONIC_AND,
					operand_reg(left_reg, bit_count),
					operand_reg(right_reg, bit_count));
			return;
		case INSTR_BIN_OR:
			encode_2(buffer,
					MNEMONIC_OR,
					operand_reg(left_reg, bit_count),
					operand_reg(right_reg, bit_count));
			return;
		case INSTR_BIN_XOR:
			encode_2(buffer,
					MNEMONIC_XOR,
					operand_reg(left_reg, bit_count),
					operand_reg(right_reg, bit_count));
			return;
		case INSTR_BIN_SHIFT_LEFT:
			_emit_bitwise_shift(buffer,
					MNEMONIC_SHL,
					left_reg,
					right_reg,
					dst_loc.reg,
					bit_count,
					// Remove `rcx` from temp registers, since it is a fixed input to the shift
					// instruction.
					_collect_available_registers(gen, instr_index) & (~(1 << X64_REG_C)),
					gen->allocator,
					gen->temp_allocator);
			return;
		case INSTR_BIN_SHIFT_RIGHT:
			_emit_bitwise_shift(buffer,
					MNEMONIC_SHR,
					left_reg,
					right_reg,
					dst_loc.reg,
					bit_count,
					// Remove `rcx` from temp registers, since it is a fixed input to the shift
					// instruction.
					_collect_available_registers(gen, instr_index) & (~(1 << X64_REG_C)),
					gen->allocator,
					gen->temp_allocator);
			return;
		}

		unreachable();
	}

	case INSTR_PTR_LOAD_8:
	case INSTR_PTR_LOAD_16:
	case INSTR_PTR_LOAD_32:
	case INSTR_PTR_LOAD_64: {
		const InstrStorageLocation dst_loc = gen->instr_storage[instr_index.value];
		const InstrStorageLocation ptr_loc = gen->instr_storage[instr->ptr_load.ptr.value];
		assert(dst_loc.kind == INSTR_STORAGE_REG);
		assert(ptr_loc.kind == INSTR_STORAGE_REG);

		uint8_t bit_count = _bit_count_from_index(instr->kind - INSTR_PTR_LOAD_8);
		encode_2(buffer,
				MNEMONIC_MOV,
				operand_reg(dst_loc.reg, bit_count),
				operand_mem(ptr_loc.reg, bit_count));
		return;
	}

	case INSTR_MEM_COPY_FIXED: {
		const InstrStorageLocation src_loc = gen->instr_storage[instr->mem_copy_fixed.src.value];
		const InstrStorageLocation dst_loc = gen->instr_storage[instr->mem_copy_fixed.dst.value];

		Operand src_operand;
		Operand dst_operand;
		if (src_loc.kind == INSTR_STORAGE_REG) {
			src_operand = operand_mem(src_loc.reg, 64);
		} else if (src_loc.kind == INSTR_STORAGE_STACK) {
			src_operand = operand_stack_mem((int32_t)src_loc.stack.offset, 64);
		}

		if (dst_loc.kind == INSTR_STORAGE_REG) {
			dst_operand = operand_mem(dst_loc.reg, 64);
		} else if (dst_loc.kind == INSTR_STORAGE_STACK) {
			dst_operand = operand_stack_mem((int32_t)dst_loc.stack.offset, 64);
		}

		uint16_t temp_registers = _collect_available_registers(gen, instr_index);
		_emit_mem_copy_fixed(buffer,
				src_operand,
				dst_operand,
				instr->mem_copy_fixed.size,
				temp_registers);
		return;
	}

	case INSTR_MEM_ZERO_FIXED: {
		uint16_t temp_registers = _collect_available_registers(gen, instr_index);
		assert(temp_registers != 0);

		const InstrStorageLocation dst_loc = gen->instr_storage[instr->mem_zero_fixed.dst.value];

		Operand dst_operand;
		if (dst_loc.kind == INSTR_STORAGE_REG) {
			dst_operand = operand_mem(dst_loc.reg, 64);
		} else if (dst_loc.kind == INSTR_STORAGE_STACK) {
			dst_operand = operand_stack_mem((int32_t)dst_loc.stack.offset, 64);
		}

		_emit_mem_zero_fixed(buffer, dst_operand, instr->mem_zero_fixed.size, temp_registers);
		return;
	}

	case INSTR_PTR_STORE_8:
	case INSTR_PTR_STORE_16:
	case INSTR_PTR_STORE_32:
	case INSTR_PTR_STORE_64: {
		const InstrStorageLocation ptr_loc = gen->instr_storage[instr->ptr_store.ptr.value];
		const InstrStorageLocation value_loc = gen->instr_storage[instr->ptr_store.value.value];
		assert(ptr_loc.kind == INSTR_STORAGE_REG);
		assert(value_loc.kind == INSTR_STORAGE_REG);

		uint8_t bit_count = _bit_count_from_index(instr->kind - INSTR_PTR_STORE_8);
		encode_2(buffer,
				MNEMONIC_MOV,
				operand_mem(ptr_loc.reg, bit_count),
				operand_reg(value_loc.reg, bit_count));
		return;
	}

	case INSTR_LOAD_ARG_8:
	case INSTR_LOAD_ARG_16:
	case INSTR_LOAD_ARG_32:
	case INSTR_LOAD_ARG_64:
	case INSTR_LOAD_ARG_STACK:
		// There is nothing to do. These instruction type is more of a hint
		// to where to look for the value, it doesn't get turned into any machine code.
		//
		// The register allocator allocates registers corresponding to the function
		// argument, and during the compilation of other instructions, the allocated
		// register is used as an input.
		return;
	
	case INSTR_STACK_ALLOC:
		return;
	
	case INSTR_STACK_ADDR: {
		const InstrStorageLocation dst_loc = gen->instr_storage[instr_index.value];
		const InstrStorageLocation alloc_loc = gen->instr_storage[instr->stack_addr.stack_alloc.value];

		assert(dst_loc.kind == INSTR_STORAGE_REG);
		assert(alloc_loc.kind == INSTR_STORAGE_STACK || alloc_loc.kind == INSTR_STORAGE_CALL_FRAME);

		if (alloc_loc.kind == INSTR_STORAGE_STACK) {
			encode_2(buffer,
					MNEMONIC_LEA,
					operand_reg(dst_loc.reg, 64),
					operand_stack_mem((int32_t)alloc_loc.stack.offset, 64));
		} else if (alloc_loc.kind == INSTR_STORAGE_CALL_FRAME) {

			ArenaRegion temp = arena_begin_temp(gen->temp_allocator);
			CallFrameLayout frame_layout = compute_call_frame_layout(
					&gen->function_signature,
					gen->temp_allocator);
			arena_end_temp(temp);

			uint32_t call_frame_offset = frame_layout.stack_slot_count * 8;

			uint32_t offset = 32 // shadow space
				+ (array_size(CDECL_CALLEE_SAVED) * 8) + 8 // call prologue + 8 bytes to align back
				                                           // to 16 bytes
				+ gen->stack_usage
				+ 8 // return address pushed by the `call` instruction
				+ alloc_loc.call_frame.slot * 8; // offset of the slot

			encode_2(buffer,
					MNEMONIC_LEA,
					operand_reg(dst_loc.reg, 64),
					operand_stack_mem((int32_t)offset, 64));
		} else {
			unreachable();
		}

		return;
	}

	case INSTR_BITWISE_NOT_8:
	case INSTR_BITWISE_NOT_16:
	case INSTR_BITWISE_NOT_32:
	case INSTR_BITWISE_NOT_64: {
		uint8_t bit_count = _bit_count_from_index(instr->kind - INSTR_BITWISE_NOT_8);

		const InstrStorageLocation dst_loc = gen->instr_storage[instr_index.value];
		const InstrStorageLocation operand_loc = gen->instr_storage[instr->negate.operand.value];

		assert(dst_loc.kind == INSTR_STORAGE_REG);
		assert(operand_loc.kind == INSTR_STORAGE_REG);

		_emit_mov_regs(buffer, operand_loc.reg, dst_loc.reg, bit_count);
		encode_1(buffer, MNEMONIC_NOT, operand_reg(dst_loc.reg, bit_count));
		return;
	}

	case INSTR_NOT:
		return;
	
	case INSTR_LOGICAL_AND:
	case INSTR_LOGICAL_OR:
		return;

	case INSTR_COMPARE_8:
	case INSTR_COMPARE_16:
	case INSTR_COMPARE_32:
	case INSTR_COMPARE_64: {
		const InstrStorageLocation left_loc = gen->instr_storage[instr->compare.left.value];
		const InstrStorageLocation right_loc = gen->instr_storage[instr->compare.right.value];
		assert(left_loc.kind == INSTR_STORAGE_REG);
		assert(right_loc.kind == INSTR_STORAGE_REG);

		uint8_t bit_count = _bit_count_from_index(instr->kind - INSTR_COMPARE_8);

		encode_2(buffer,
				MNEMONIC_CMP,
				operand_reg(left_loc.reg, bit_count),
				operand_reg(right_loc.reg, bit_count));
		return;
	}
	case INSTR_BOOL_TO_INT: {
		const InstrStorageLocation dst_loc = gen->instr_storage[instr_index.value];
		assert(dst_loc.kind == INSTR_STORAGE_REG);

		Instr* operand_instr = instr_buffer_at(instr_buffer, instr->bool_to_int.operand);
		assert(has_flag(INSTR_FEATURES[operand_instr->kind], INSTR_FEATURE_BOOL));
		
		// Consume all the `INSTR_NOT`
		bool condition_is_flipped = 0;
		while (operand_instr->kind == INSTR_NOT) {
			condition_is_flipped = !condition_is_flipped;
			operand_instr = instr_buffer_at(instr_buffer, operand_instr->not.operand);
		}

		assert(operand_instr->kind >= INSTR_COMPARE_8 && operand_instr->kind <= INSTR_COMPARE_64);
		InstrCompareKind compare_kind = operand_instr->compare.kind;
		if (condition_is_flipped) {
			compare_kind = instr_compare_kind_flip(compare_kind);
		}

		MnemonicKind mnemonic = 0;
		switch (compare_kind) {
		case INSTR_CMP_EQUAL:
			mnemonic = MNEMONIC_SETZ;
			break;
		case INSTR_CMP_NOT_EQUAL:
			mnemonic = MNEMONIC_SETNZ;
			break;
		case INSTR_CMP_LESS:
			mnemonic = MNEMONIC_SETL;
			break;
		case INSTR_CMP_LESS_OR_EQUAL:
			mnemonic = MNEMONIC_SETLE;
			break;
		case INSTR_CMP_GREATER:
			mnemonic = MNEMONIC_SETNLE;
			break;
		case INSTR_CMP_GREATER_OR_EQUAL:
			mnemonic = MNEMONIC_SETNL;
			break;
		}

		assert(mnemonic != 0);

		encode_1(buffer, mnemonic, operand_reg(dst_loc.reg, 8));
		return;							
	}
	
	case INSTR_NEGATE_8:
	case INSTR_NEGATE_16:
	case INSTR_NEGATE_32:
	case INSTR_NEGATE_64: {
		uint8_t bit_count = _bit_count_from_index(instr->kind - INSTR_NEGATE_8);

		const InstrStorageLocation dst_loc = gen->instr_storage[instr_index.value];
		const InstrStorageLocation operand_loc = gen->instr_storage[instr->negate.operand.value];

		assert(dst_loc.kind == INSTR_STORAGE_REG);
		assert(operand_loc.kind == INSTR_STORAGE_REG);

		_emit_mov_regs(buffer, operand_loc.reg, dst_loc.reg, bit_count);
		encode_1(buffer, MNEMONIC_NEG, operand_reg(dst_loc.reg, bit_count));
		return;
	}
	
	case INSTR_UNSIGNED_EXTEND_TO_16:
	case INSTR_UNSIGNED_EXTEND_TO_32:
	case INSTR_UNSIGNED_EXTEND_TO_64:

	case INSTR_SIGNED_EXTEND_TO_16:
	case INSTR_SIGNED_EXTEND_TO_32:
	case INSTR_SIGNED_EXTEND_TO_64: {
		const InstrStorageLocation dst_loc = gen->instr_storage[instr_index.value];
		const InstrStorageLocation src_loc = gen->instr_storage[instr->extend.value.value];
		assert(dst_loc.kind == INSTR_STORAGE_REG);
		assert(src_loc.kind == INSTR_STORAGE_REG);

		uint8_t value_bit_count = instr->extend.value_bit_count;

		if (instr->kind >= INSTR_UNSIGNED_EXTEND_TO_16
				&& instr->kind <= INSTR_UNSIGNED_EXTEND_TO_64) {

			uint8_t output_bit_count = 8 << (instr->kind - INSTR_UNSIGNED_EXTEND_TO_16 + 1);
			assert(value_bit_count < output_bit_count);

			if (value_bit_count == 8 || value_bit_count == 16) {
				encode_2(buffer,
						MNEMONIC_MOVZX,
						operand_reg(dst_loc.reg, output_bit_count),
						operand_reg(src_loc.reg, value_bit_count));
			} else {
				assert(value_bit_count == 32);
				assert(output_bit_count == 64);
				// NOTE: Moving (writing) to a 32-bit register zeros out the upper half of the
				//       corresponding 64-bit regiter.
				//       There is no `movzx` for zero extending 32-bit value to a 64-bit one.
				encode_2(buffer,
						MNEMONIC_MOV,
						operand_reg(dst_loc.reg, value_bit_count),
						operand_reg(src_loc.reg, value_bit_count));
			}
		} else {
			assert(instr->kind >= INSTR_SIGNED_EXTEND_TO_16
					&& instr->kind <= INSTR_SIGNED_EXTEND_TO_64);

			uint8_t output_bit_count = 8 << (instr->kind - INSTR_SIGNED_EXTEND_TO_16 + 1);
			assert(value_bit_count < output_bit_count);

			if (value_bit_count == 8 || value_bit_count == 16) {
				encode_2(buffer,
						MNEMONIC_MOVSX,
						operand_reg(dst_loc.reg, output_bit_count),
						operand_reg(src_loc.reg, value_bit_count));
			} else {
				assert(value_bit_count == 32);
				assert(output_bit_count == 64);
				encode_2(buffer,
						MNEMONIC_MOVSXD,
						operand_reg(dst_loc.reg, output_bit_count),
						operand_reg(src_loc.reg, value_bit_count));
			}
		}

		return;
	}


	case INSTR_REDUCE_TO_8:
	case INSTR_REDUCE_TO_16:
	case INSTR_REDUCE_TO_32: {
		const InstrStorageLocation dst_loc = gen->instr_storage[instr_index.value];
		const InstrStorageLocation src_loc = gen->instr_storage[instr->reduce.value.value];
		assert(dst_loc.kind == INSTR_STORAGE_REG);
		assert(src_loc.kind == INSTR_STORAGE_REG);

		uint8_t output_bit_count = 8 << (instr->kind - INSTR_REDUCE_TO_8);

		encode_2(buffer,
				MNEMONIC_MOV,
				operand_reg(dst_loc.reg, output_bit_count),
				operand_reg(src_loc.reg, output_bit_count));
		return;
	}

	case INSTR_BRANCH: {
		const Instr* condition_instr = instr_buffer_at(instr_buffer, instr->branch.condition);

		assert_msg(
				has_flag(INSTR_FEATURES[condition_instr->kind], INSTR_FEATURE_BOOL),
				"A condition instruction for `INSTR_BRANCH` must produce a boolean value");

		_x64_generate_phi_copies(gen, region_id, buffer);
		return;
	}
	case INSTR_JUMP: {
		_x64_generate_phi_copies(gen, region_id, buffer);
		return;
	}

	case INSTR_RETURN_VALUE: {
		assert(gen->function_signature.returns != NULL);

		InstrIndex return_value = instr->return_value.value;
		const InstrStorageLocation return_value_loc = gen->instr_storage[return_value.value];
		const AbiParam* returns = gen->function_signature.returns;
		if (returns->kind == ABI_PARAM_NORMAL
				|| (returns->kind == ABI_PARAM_STRUCT && returns->struct_size <= 8)) {
			assert(return_value_loc.kind == INSTR_STORAGE_REG);

			_emit_mov_regs(buffer, return_value_loc.reg, X64_REG_A, 64);
		} else if (returns->kind == ABI_PARAM_STRUCT && returns->struct_size > 8) {
			Operand src_operand;
			if (return_value_loc.kind == INSTR_STORAGE_STACK) {
				src_operand = operand_stack_mem((int32_t)return_value_loc.stack.offset, 64);
			} else if (return_value_loc.kind == INSTR_STORAGE_REG) {
				src_operand = operand_mem(return_value_loc.reg, 64);
			}

			Operand dst_operand = operand_mem(CDECL_ARG_REGS[0], 64);

			uint16_t temp_registers = _collect_available_registers(gen, instr_index);
			_emit_mem_copy_fixed(buffer,
					src_operand,
					dst_operand,
					gen->function_signature.returns->struct_size,
					temp_registers);
		} else {
			panic("Invalid `AbiParamKind` for the return value of the function");
		}

		_emit_callee_epilogue(gen, buffer);

		// Don't need to generate a `ret` instruction, since it is done later when the control
		// instructions at the end of each code block are generated
		return;
	}
	case INSTR_RET:
		assert(gen->function_signature.returns == NULL);
		_emit_callee_epilogue(gen, buffer);

		// Don't need to generate a `ret` instruction, since it is done later when the control
		// instructions at the end of each code block are generated
		return;
	
	case INSTR_IO_STATE:
		return;
	
	case INSTR_CALL_INDIRECT:
		_lower_call(gen,
				instr_index,
				region_id,
				buffer,
				(CallLoweringOptions) {
					.is_direct = false,
					.args = instr->call_indirect.args,
					.callee_signature_index = instr->call_indirect.signature_index,
					.indirect = {
						.address_instr = instr->call_indirect.function_addr,
					}
				});
		return;
	case INSTR_CALL_DIRECT:
		_lower_call(gen,
				instr_index,
				region_id,
				buffer,
				(CallLoweringOptions) {
					.is_direct = true,
					.args = instr->call_direct.args,
					.callee_signature_index = instr->call_direct.signature_index,
					.direct = {
						.callee_index = instr->call_direct.function_index,
					}
				});
		return;
	
	case INSTR_LOAD_FUNCTION_ADDR: {
		assert(instr_storage.kind == INSTR_STORAGE_REG);
		CallAddressPlaceholder* addr_placeholder = NULL;

		{
			assert(gen->call_addr_placeholder_count < gen->call_addr_placeholder_capacity);
			size_t index = gen->call_addr_placeholder_count;
			gen->call_addr_placeholder_count += 1;

			addr_placeholder = &gen->call_addr_placeholders[index];
			gen->call_addr_placeholder_regions[index] = region_id;
		}

		encode_2(buffer,
				MNEMONIC_LEA,
				operand_reg(instr_storage.reg, 64),
				operand_rip_relative(0, 64));

		addr_placeholder->instruction_end_offset = buffer->size;
		addr_placeholder->addr_offset = buffer->size - 4;
		addr_placeholder->function_index = instr->load_function_addr.function_index;
		addr_placeholder->kind = CALL_ADDR_RELATIVE;
		return;
	}
	case INSTR_LOAD_EXTERNAL_FUNCTION_ADDR: {
		assert(instr_storage.kind == INSTR_STORAGE_REG);
		CallAddressPlaceholder* addr_placeholder = NULL;

		{
			assert(gen->call_addr_placeholder_count < gen->call_addr_placeholder_capacity);
			size_t index = gen->call_addr_placeholder_count;
			gen->call_addr_placeholder_count += 1;

			addr_placeholder = &gen->call_addr_placeholders[index];
			gen->call_addr_placeholder_regions[index] = region_id;
		}

		encode_2(buffer, MNEMONIC_MOV, operand_reg(instr_storage.reg, 64), operand_imm(0, 64));

		addr_placeholder->instruction_end_offset = buffer->size;
		addr_placeholder->addr_offset = buffer->size - 8;
		addr_placeholder->function_index = instr->load_function_addr.function_index;
		addr_placeholder->kind = CALL_ADDR_ABSOLUTE;
		return;
	}

	case INSTR_REGION:
		panic("`INSTR_REGION` are handled outside of this functions. If this `panic` has been"
				" reached, it means this function was accidentally called for a region");
	case INSTR_PHI:
		// Nothing to do here, everything is already handled during code gen of `INSTR_REGION`
		return;
	case INSTR_SELECT:
		return;
	case INSTR_COUNT:
		unreachable();
	}

	unreachable();
}

static size_t _compute_control_instr_encoding_size(const Instr* instr) {
	switch (instr->kind) {
	case INSTR_JUMP:
		return compute_encoding_size_1(MNEMONIC_JMP, operand_rel32(0));
	case INSTR_BRANCH:
		// A branch gets encoded as two jumps:
		// 1. Jump to the true region if condition is true
		// 2. Jump to the false region otherwise
		return compute_encoding_size_1(MNEMONIC_JZ, operand_rel32(0))
			+ compute_encoding_size_1(MNEMONIC_JMP, operand_rel32(0));
	case INSTR_RETURN_VALUE:
	case INSTR_RET:
		return 1;
	default:
		unreachable();
	}

	return 0;
}

static MnemonicKind _select_jmp_mnemonic(InstrCompareKind op) {
	switch (op) {
	case INSTR_CMP_EQUAL:
		return MNEMONIC_JZ;
	case INSTR_CMP_NOT_EQUAL:
		return MNEMONIC_JNZ;
	case INSTR_CMP_LESS:
		return MNEMONIC_JL;
	case INSTR_CMP_LESS_OR_EQUAL:
		return MNEMONIC_JLE;
	case INSTR_CMP_GREATER:
		return MNEMONIC_JNLE;
	case INSTR_CMP_GREATER_OR_EQUAL:
		return MNEMONIC_JNL;
	}

	unreachable();
	return 0;
}

static void _encode_control_instr(const Instr* instr,
		const InstrBuffer* instr_buffer,
		size_t current_block_end_offset,
		const size_t* code_block_offsets,
		CodeBuffer* buffer) {
	switch (instr->kind) {
	case INSTR_JUMP: {
		uint16_t target_region_id = instr_region_id(instr_buffer, instr->jump.target_region);
		size_t target_offset = code_block_offsets[target_region_id];
		assert(target_offset <= INT64_MAX);

		int64_t relative_offset = (int64_t)target_offset - ((int64_t)current_block_end_offset + 5);
		assert(relative_offset >= INT32_MIN);
		assert(relative_offset <= INT32_MAX);

		encode_1(buffer, MNEMONIC_JMP, operand_rel32((int32_t)relative_offset));
		break;
	}
	case INSTR_BRANCH: {
		// A branch gets encoded as two jumps:
		// 1. Jump to the true region if condition is true
		// 2. Jump to the false region otherwise

		MnemonicKind jump_to_true_mnemonic_kind = 0;

		const Instr* condition_instr = instr_buffer_at(instr_buffer, instr->branch.condition);

		bool condition_is_flipped = false;
		while (condition_instr->kind == INSTR_NOT) {
			condition_is_flipped = !condition_is_flipped;
			condition_instr = instr_buffer_at(instr_buffer, condition_instr->not.operand);
		}

		switch (condition_instr->kind) {
		case INSTR_COMPARE_8:
		case INSTR_COMPARE_16:
		case INSTR_COMPARE_32:
		case INSTR_COMPARE_64: 
			if (condition_is_flipped) {
				jump_to_true_mnemonic_kind = _select_jmp_mnemonic(
						instr_compare_kind_flip(condition_instr->compare.kind));
			} else {
				jump_to_true_mnemonic_kind = _select_jmp_mnemonic(condition_instr->compare.kind);
			}
			break;
		default:
			jump_to_true_mnemonic_kind = condition_is_flipped ? MNEMONIC_JNZ : MNEMONIC_JZ;
		}

		uint16_t true_region_id = instr_region_id(instr_buffer, instr->branch.true_region);
		size_t true_offset = code_block_offsets[true_region_id];
		assert(true_offset <= INT64_MAX);

		int64_t true_relative_offset = (int64_t)true_offset - ((int64_t)current_block_end_offset + 6);
		assert(true_relative_offset >= INT32_MIN);
		assert(true_relative_offset <= INT32_MAX);

		uint16_t false_region_id = instr_region_id(instr_buffer, instr->branch.false_region);
		size_t false_offset = code_block_offsets[false_region_id];
		assert(false_offset <= INT64_MAX);

		int64_t false_relative_offset = (int64_t)false_offset - ((int64_t)current_block_end_offset + 6 + 5);
		assert(false_relative_offset >= INT32_MIN);
		assert(false_relative_offset <= INT32_MAX);

		encode_1(buffer, jump_to_true_mnemonic_kind, operand_rel32((int32_t)true_relative_offset));
		encode_1(buffer, MNEMONIC_JMP, operand_rel32((int32_t)false_relative_offset));
		break;
	}
	case INSTR_RETURN_VALUE:
	case INSTR_RET:
		encode_n(buffer, MNEMONIC_RET, NULL, 0);
		break;
	default:
		unreachable();
	}

	return;
}

//
// Code Generation Stages
//

static void _run_reg_allocator(X64CodeGenerator* gen, InstrIndexArray scheduled_instr) {
	uint16_t allowed_registers = UINT16_MAX;
	allowed_registers &= ~(1 << X64_REG_SP);
	allowed_registers &= ~(1 << X64_REG_BP);

	// HACK: Some times the register allocator might allocate the whole register
	//       to some instruction and also it's high part to the other, thus any
	//       writes by any of the two instructions will be reflected in two places.
	allowed_registers &= ~(1 << X64_REG_SI);
	allowed_registers &= ~(1 << X64_REG_DI);

	RegisterAllocationResult result;
	result = x64_alloc_regs(&gen->instr_buffer,
			scheduled_instr,
			gen->live_ranges,
			allowed_registers,
			// Use `temp_allocator` as a persistent one, since register allocations are only used
			// within the backend
			&gen->function_signature,
			gen->function_call_signatures,
			gen->temp_allocator,
			gen->allocator);

	gen->instr_storage = result.allocations;
	gen->interference_graph = result.interference_graph;
	gen->stack_usage = align(result.stack_usage, 16);

	if (has_flag(gen->flags, X64_PRINT_ASSIGNED_STORAGE_LOC)) {
		printf("Assigned storage locations:\n");
		for (size_t i = 0; i < gen->instr_buffer.count; i += 1) {
			ArenaRegion temp = arena_begin_temp(gen->temp_allocator);

			String storage_string = STR_LIT("none");

			if (gen->instr_storage[i].kind == INSTR_STORAGE_REG) {
				StringBuilder builder = { .arena = gen->temp_allocator };

				const InstrKind instr_kind = gen->instr_buffer.instr[i].kind;

				_format_reg_name(&builder,
						gen->instr_storage[i].reg,
						s_instr_value_bit_count[instr_kind]);

				storage_string = builder.string;
			} else if (gen->instr_storage[i].kind == INSTR_STORAGE_STACK) {
				StringBuilder builder = { .arena = gen->temp_allocator };

				str_builder_append(&builder, STR_LIT("stack + "));
				str_builder_append_int(&builder, gen->instr_storage[i].stack.offset);

				storage_string = builder.string;
			} else if (gen->instr_storage[i].kind == INSTR_STORAGE_CALL_FRAME) {
				StringBuilder builder = { .arena = gen->temp_allocator };

				str_builder_append(&builder, STR_LIT("call_frame + "));
				str_builder_append_int(&builder, gen->instr_storage[i].call_frame.slot * 8);

				storage_string = builder.string;
			}

			printf("%zu: %.*s\n", i, STR_FMT(storage_string));

			arena_end_temp(temp);
		}

		// Gather stats
		uint32_t max_register_pressure = 0;
		uint16_t used_registers = 0;

		for (uint16_t i = 0; i < gen->instr_buffer.count; i += 1) {
			if (result.allocations[i].kind == INSTR_STORAGE_REG) {
				used_registers |= 1 << result.allocations[i].reg;
			}

			InstrIndexArray edges = result.interference_graph[i];

			uint16_t interfering_regs = 0;
			for (size_t edge_index = 0; edge_index < edges.count; edge_index += 1) {
				InstrIndex interfering_instr = edges.instr[edge_index];

				if (result.allocations[interfering_instr.value].kind != INSTR_STORAGE_REG) {
					continue;
				}

				interfering_regs |= (1 << result.allocations[interfering_instr.value].reg);
			}

			uint32_t pressure = 0;
			for (uint16_t r = 0; r < X64_REG_COUNT; r += 1) {
				if ((interfering_regs & (1 << r))) {
					pressure += 1;
				}
			}

			max_register_pressure = max(max_register_pressure, pressure);
		}

		ArenaRegion temp = arena_begin_temp(gen->temp_allocator);

		StringBuilder builder = { .arena = gen->temp_allocator };

		for (uint16_t i = 0; i < X64_REG_COUNT; i += 1) {
			if ((used_registers & (1 << i)) == 0) {
				continue;
			}

			_format_reg_name(&builder,
					(X64Register)i,
					64);
			str_builder_append_char(&builder, ' ');
		}

		printf("\nRegister allocation stats:\n");
		printf("max register pressure: %u\n", max_register_pressure);
		printf("       used registers: %.*s\n", STR_FMT(builder.string));
		printf("          stack usage: %u\n", result.stack_usage);

		arena_end_temp(temp);
	}
}

static void _merge_string_consts(X64CodeGenerator* gen) {
	profile_scope_start(__func__);

	StringArray strings = gen->string_consts;

	gen->string_offsets = arena_alloc_array(gen->temp_allocator, size_t, strings.count);
	gen->merged_strings_buffer = arena_alloc_array(gen->allocator, char, 0);

	for (size_t i = 0; i < strings.count; i += 1) {
		size_t string_length = strings.values[i].length;

		// +1 for null-terminator
		char* string = arena_alloc_array(gen->allocator, char, string_length + 1);
		memcpy(string, strings.values[i].v, string_length);
		string[string_length] = 0;

		gen->string_offsets[i] = string - gen->merged_strings_buffer;
	}

	profile_scope_end();
}

//
// Instruction Scheduler
//
// Instruction scheduler is resposible for assigning each instruction to one of the regions in such
// a way, that whenever an instruction is about to execute, all of its inputs are guaranteed to be
// available.
//

static void _collect_phis(X64CodeGenerator* gen, Arena* allocator) {
	profile_scope_start(__func__);
	const InstrBuffer* instr_buffer = &gen->instr_buffer;

	uint16_t* phi_variant_counts_per_region = arena_alloc_array_zeroed(allocator,
			uint16_t,
			gen->instr_buffer.region_count);

	for (uint16_t i = 0; i < instr_buffer->count; i += 1) {
		const Instr* instr = &instr_buffer->instr[i];
		if (instr->kind != INSTR_PHI) {
			continue;
		}

		InstrInputs variants = instr->phi.variants;
		assert(variants.start <= instr_buffer->inputs_buffer_size);
		assert(variants.start + variants.count <= instr_buffer->inputs_buffer_size);

		for (uint16_t j = 0; j < variants.count; j += 1) {
			InstrIndex select = instr_buffer->inputs_buffer[variants.start + j];
			const Instr* select_instr = instr_buffer_at(instr_buffer, select);

			uint16_t region_id = instr_region_id(instr_buffer, select_instr->select.region);
			phi_variant_counts_per_region[region_id] += 1;

		}
	}

	InstrIndexArray* phi_variants_per_region = arena_alloc_array_zeroed(allocator,
			InstrIndexArray,
			instr_buffer->region_count);

	InstrIndex** phi_node_of_variant = arena_alloc_array(allocator,
			InstrIndex*,
			instr_buffer->region_count);

	for (uint16_t i = 0; i < gen->instr_buffer.region_count; i += 1) {
		phi_variants_per_region[i].instr = arena_alloc_array(allocator,
				InstrIndex,
				phi_variant_counts_per_region[i]);
	}

	for (uint16_t i = 0; i < gen->instr_buffer.region_count; i += 1) {
		phi_node_of_variant[i] = arena_alloc_array(allocator,
				InstrIndex,
				phi_variant_counts_per_region[i]);
	}

	// Sort all the phi variants into the arrays corresponding to the region where the variant must
	// be placed.
	for (uint16_t i = 0; i < instr_buffer->count; i += 1) {
		const Instr* instr = &instr_buffer->instr[i];
		if (instr->kind != INSTR_PHI) {
			continue;
		}

		InstrInputs variants = instr->phi.variants;
		for (uint16_t j = 0; j < variants.count; j += 1) {
			InstrIndex select = instr_buffer->inputs_buffer[variants.start + j];
			const Instr* select_instr = instr_buffer_at(instr_buffer, select);

			uint16_t region_id = instr_region_id(instr_buffer, select_instr->select.region);
			InstrIndexArray* variants = &phi_variants_per_region[region_id];

			variants->instr[variants->count] = select_instr->select.value;
			phi_node_of_variant[region_id][variants->count] = (InstrIndex) { i };
			variants->count += 1;
		}
	}

	if (has_flag(gen->flags, X64_DEBUG_LOG)) {
		for (uint16_t region_id = 0; region_id < instr_buffer->region_count; region_id += 1) {
			uint16_t phi_count = phi_variant_counts_per_region[region_id];
			printf("%u -> %u:\n", (uint32_t)region_id, (uint32_t)phi_count);

			for (uint16_t i = 0; i < phi_count; i += 1) {
				printf("phi: %u variant: %u\n",
						(uint32_t)phi_node_of_variant[region_id][i].value,
						(uint32_t)phi_variants_per_region[region_id].instr[i].value);
			}
		}
	}

	gen->phi_variant_counts_per_region = phi_variant_counts_per_region;
	gen->phi_variants_per_region = phi_variants_per_region;
	gen->phi_node_of_variant = phi_node_of_variant;

	profile_scope_end();
}

static bool _validate_instr_scheduling_for_region(const InstrBuffer* instr_buffer,
		uint16_t* instr_scheduled_region, 
		uint16_t* instr_position_in_region,
		uint16_t current_region_id,
		InstrIndexArray scheduled,
		const CFGDominatorTree* dom_tree,
		Arena* temp_allocator) {
	profile_scope_start(__func__);
	
	assert(scheduled.count > 0);

	bool valid = true;

	if (!instr_is_control(instr_buffer, scheduled.instr[scheduled.count - 1])) {
		const Instr* last_instr = instr_buffer_at(instr_buffer, scheduled.instr[scheduled.count - 1]);
		debug_log_error(
				"The last instruction in the region must be a control instruction.\n"
				"The last instruction of the region '%u' is '%.*s'\n",
				current_region_id,
				STR_FMT(instr_name(last_instr->kind)));

		valid = false;
	}

	for (size_t i = 0; i < scheduled.count; i += 1) {
		InstrIndex current_instr = scheduled.instr[i];
		const Instr* instr = instr_buffer_at(instr_buffer, scheduled.instr[i]);

		if (instr->kind == INSTR_SELECT) {
			uint16_t region_assigned_to_input = instr_scheduled_region[instr->select.value.value];
			uint16_t expected_region = instr_region_id(instr_buffer, instr->select.region);

			bool input_is_available = dom_tree_is_region_dominated_by(dom_tree,
					region_assigned_to_input,
					expected_region);

			if (!input_is_available) {
				debug_log_error(
						"Value definition '%u' appears before its input '%u'. "
						"Input is not available in region with id '%u', since it is placed in"
						" region with id '%u'",
						(uint32_t)scheduled.instr[i].value,
						(uint32_t)instr->select.value.value,
						expected_region,
						region_assigned_to_input);

				valid = false;
			}
		} else {
			ArenaRegion inner_temp = arena_begin_temp(temp_allocator);

			InstrQueue queue;
			instr_queue_alloc(&queue, temp_allocator, instr_buffer->count);

			instr_enumerate_uses(instr_buffer, scheduled.instr[i], &queue);

			for (size_t j = 0; j < queue.count; j += 1) {
				InstrIndex input = queue.buffer[j];
				if (input.value == INVALID_INSTR_INDEX.value) {
					continue;
				}

				const Instr* input_instr = instr_buffer_at(instr_buffer, input);
				if (input_instr->kind == INSTR_REGION) {
					continue;
				}

				bool input_is_available = true;
				if (instr_scheduled_region[input.value] == current_region_id) {
					// The input and the current instruction are in the same region.
					// 
					// Check whether the input is placed before the def (current instruction).

					uint16_t current_instr_position = instr_position_in_region[current_instr.value];
					uint16_t input_instr_position = instr_position_in_region[input.value];

					input_is_available = input_instr_position < current_instr_position;
				} else {
					// Input and def are in different regions.
					//
					// The input is only then available, when the input's region is dominated by
					// current instruction's region.
					input_is_available = dom_tree_is_region_dominated_by(dom_tree,
							instr_scheduled_region[input.value],
							current_region_id);

				}

				if (input_is_available) {
					continue;
				}

				debug_log_error(
						"Input '%u' for value definition '%u' is not available on this "
						"control path\n"
						"\n"
						"Def '%u' is scheduled to execute in region with id '%u' at index '%u'\n"
						"Input '%u' is scheduled to execute in region with id '%u' at index '%u'\n",
						(uint32_t)input.value,
						(uint32_t)scheduled.instr[i].value,

						(uint32_t)scheduled.instr[i].value,
						(uint32_t)current_region_id,
						(uint32_t)instr_position_in_region[scheduled.instr[i].value],

						(uint32_t)input.value,
						(uint32_t)instr_scheduled_region[input.value],
						(uint32_t)instr_position_in_region[input.value]);

				valid = false;
			}

			arena_end_temp(inner_temp);
		}
	}
	
	profile_scope_end();
	return valid;
}

// A position of an instruction that was assigned during scheduling
typedef struct {
	// The region this instruction should appear in.
	uint16_t region_id;

	// The position in the above region. Lower value means the instruction must appear at the start,
	// higher value - at the end. It is like a reverse index 
	uint16_t order_in_region;
} InstrPosition;

typedef struct {
	const InstrBuffer* instr_buffer;
	InstrPosition* positions;
	const CFGDominatorTree* dom_tree;

	Arena* temp_allocator;

	// For each region stores a monotonically increasing integer.
	// This integer tells the order in which each instruction was added to this region.
	uint16_t* next_position_in_region;
	InstrIndex def;
} InstrSchedulingContext;

// Checks whether the `input_instr_index` is guaranteed to be available at position
// `expected_position`
//
// If it's not, uplifts `input_instr_index`. And pushes it onto the queue, since now
// `input_instr_index` and it's dependencies need to be rescheduled.
static void _try_enqueue_for_scheduling(InstrQueue* queue,
		InstrSchedulingContext* context,
		InstrPosition expected_position,
		InstrIndex input_instr_index) {
	profile_scope_start(__func__);

	InstrPosition* input_position = &context->positions[input_instr_index.value];

	if (input_position->region_id == UINT16_MAX) {
		uint16_t order_in_region = context->next_position_in_region[expected_position.region_id];
		context->next_position_in_region[expected_position.region_id] += 1;

		input_position->region_id = expected_position.region_id;
		input_position->order_in_region = order_in_region;
		instr_queue_push_front(queue, input_instr_index);

		profile_scope_end();
		return;
	}

	bool is_input_region_dominated = dom_tree_is_region_dominated_by(context->dom_tree,
			input_position->region_id,
			expected_position.region_id);

	// The input is guaranteed to appear before the `expected_position`
	if (is_input_region_dominated
			&& input_position->order_in_region > expected_position.order_in_region) {
		profile_scope_end();
		return;
	}

	// The input is not guaranteed to appear before `expected_position`, so we need to uplift it to
	// the common region.
	uint16_t common_region_id = dom_tree_find_control_flow_split(context->dom_tree,
			input_position->region_id,
			expected_position.region_id,
			context->temp_allocator);

	uint16_t order_in_region = context->next_position_in_region[common_region_id];
	context->next_position_in_region[common_region_id] += 1;

	input_position->region_id = common_region_id;
	input_position->order_in_region = order_in_region;

	// Since we've uplifted this instruction, now its inputs have be rescheduled
	instr_queue_push_front(queue, input_instr_index);
	profile_scope_end();
}

static void _enqueue_inputs_for_scheduling(InstrQueue* queue,
		InstrIndex instr_index,
		InstrSchedulingContext* context) {
	profile_scope_start(__func__);
	const InstrBuffer* instr_buffer = context->instr_buffer;
	const Instr* instr = instr_buffer_at(instr_buffer, instr_index);

	InstrPosition current_position = context->positions[instr_index.value];

	assert_msg(current_position.region_id != UINT16_MAX,
			"Instr at `instr_index` must have an already assigned region id");

	uint16_t max_valid_order = context->next_position_in_region[current_position.region_id];
	assert_msg(current_position.order_in_region < max_valid_order,
			"Instr at `instr_index` has invalid position in the assigned region");

	switch (instr->kind) {
	case INSTR_NO_OP:
	case INSTR_UNINITIALIZED_8:
	case INSTR_UNINITIALIZED_16:
	case INSTR_UNINITIALIZED_32:
	case INSTR_UNINITIALIZED_64:
	case INSTR_CONST_8:
	case INSTR_CONST_16:
	case INSTR_CONST_32:
	case INSTR_CONST_64:
	case INSTR_CONST_STRING:
		break;
	case INSTR_BIN_OP_8:
	case INSTR_BIN_OP_16:
	case INSTR_BIN_OP_32:
	case INSTR_BIN_OP_64:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->bin_op.left);
		_try_enqueue_for_scheduling(queue, context, current_position, instr->bin_op.right);
		break;
	case INSTR_PTR_LOAD_8:
	case INSTR_PTR_LOAD_16:
	case INSTR_PTR_LOAD_32:
	case INSTR_PTR_LOAD_64:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->ptr_load.io_state);
		_try_enqueue_for_scheduling(queue, context, current_position, instr->ptr_load.ptr);
		break;
	case INSTR_PTR_STORE_8:
	case INSTR_PTR_STORE_16:
	case INSTR_PTR_STORE_32:
	case INSTR_PTR_STORE_64:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->ptr_store.io_state);
		_try_enqueue_for_scheduling(queue, context, current_position, instr->ptr_store.ptr);
		_try_enqueue_for_scheduling(queue, context, current_position, instr->ptr_store.value);
		break;
	case INSTR_MEM_COPY_FIXED:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->mem_copy_fixed.io_state);
		_try_enqueue_for_scheduling(queue, context, current_position, instr->mem_copy_fixed.src);
		_try_enqueue_for_scheduling(queue, context, current_position, instr->mem_copy_fixed.dst);
		break;
	case INSTR_MEM_ZERO_FIXED:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->mem_zero_fixed.io_state);
		_try_enqueue_for_scheduling(queue, context, current_position, instr->mem_zero_fixed.dst);
		break;
	case INSTR_LOAD_ARG_8:
	case INSTR_LOAD_ARG_16:
	case INSTR_LOAD_ARG_32:
	case INSTR_LOAD_ARG_64:
	case INSTR_LOAD_ARG_STACK:
		break;
	case INSTR_STACK_ALLOC:
		break;
	case INSTR_STACK_ADDR:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->stack_addr.stack_alloc);
		break;
	case INSTR_NOT:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->not.operand);
		break;
	case INSTR_LOGICAL_AND:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->logical_and.left);
		_try_enqueue_for_scheduling(queue, context, current_position, instr->logical_and.right);
		break;
	case INSTR_LOGICAL_OR:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->logical_or.left);
		_try_enqueue_for_scheduling(queue, context, current_position, instr->logical_or.right);
		break;
	case INSTR_COMPARE_8:
	case INSTR_COMPARE_16:
	case INSTR_COMPARE_32:
	case INSTR_COMPARE_64:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->compare.left);
		_try_enqueue_for_scheduling(queue, context, current_position, instr->compare.right);
		break;
	case INSTR_BOOL_TO_INT:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->bool_to_int.operand);
		break;
	case INSTR_NEGATE_8:
	case INSTR_NEGATE_16:
	case INSTR_NEGATE_32:
	case INSTR_NEGATE_64:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->negate.operand);
		break;
	case INSTR_BITWISE_NOT_8:
	case INSTR_BITWISE_NOT_16:
	case INSTR_BITWISE_NOT_32:
	case INSTR_BITWISE_NOT_64:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->bitwise_not.operand);
		break;
	case INSTR_SIGNED_EXTEND_TO_16:
	case INSTR_SIGNED_EXTEND_TO_32:
	case INSTR_SIGNED_EXTEND_TO_64:
	case INSTR_UNSIGNED_EXTEND_TO_16:
	case INSTR_UNSIGNED_EXTEND_TO_32:
	case INSTR_UNSIGNED_EXTEND_TO_64:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->extend.value);
		break;
	case INSTR_REDUCE_TO_8:
	case INSTR_REDUCE_TO_16:
	case INSTR_REDUCE_TO_32:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->reduce.value);
		break;
	case INSTR_BRANCH:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->branch.io_state);
		_try_enqueue_for_scheduling(queue, context, current_position, instr->branch.condition);
		break;
	case INSTR_JUMP:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->jump.io_state);
		break;
	case INSTR_RETURN_VALUE:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->return_value.io_state);
		_try_enqueue_for_scheduling(queue, context, current_position, instr->return_value.value);
		break;
	case INSTR_RET:
		_try_enqueue_for_scheduling(queue, context, current_position, instr->ret.io_state);
		break;
	case INSTR_IO_STATE:
		if (instr->io_state.producer.value != INVALID_INSTR_INDEX.value) {
			_try_enqueue_for_scheduling(queue, context, current_position, instr->io_state.producer);
		}

		break;
	case INSTR_CALL_DIRECT: {
		_try_enqueue_for_scheduling(queue, context, current_position, instr->call_indirect.io_state);

		InstrInputs args = instr->call_indirect.args;
		for (uint16_t i = 0; i < args.count; i += 1) {
			InstrIndex arg_instr = instr_buffer->inputs_buffer[args.start + i];
			_try_enqueue_for_scheduling(queue, context, current_position, arg_instr);
		}

		break;
	}
	case INSTR_CALL_INDIRECT: {
		_try_enqueue_for_scheduling(queue, context, current_position, instr->call_indirect.io_state);
		_try_enqueue_for_scheduling(queue, context, current_position, instr->call_indirect.function_addr);

		InstrInputs args = instr->call_indirect.args;
		for (uint16_t i = 0; i < args.count; i += 1) {
			InstrIndex arg_instr = instr_buffer->inputs_buffer[args.start + i];
			_try_enqueue_for_scheduling(queue, context, current_position, arg_instr);
		}

		break;
	}
	case INSTR_LOAD_FUNCTION_ADDR:
	case INSTR_LOAD_EXTERNAL_FUNCTION_ADDR:
		break;
	case INSTR_REGION:
		unreachable();
	case INSTR_PHI: {
		InstrInputs variants = instr->phi.variants;
		for (uint16_t i = 0; i < variants.count; i += 1) {
			InstrIndex variant = instr_buffer->inputs_buffer[variants.start + i];
			_try_enqueue_for_scheduling(queue, context, current_position, variant);
		}
		break;
	}
	case INSTR_SELECT: {
		uint16_t region_id = instr_region_id(instr_buffer, instr->select.region);
		_try_enqueue_for_scheduling(queue,
				context,
				// NOTE: Here we expect the value to be available at the END of the region with id
				//       `region_id`
				(InstrPosition) { .region_id = region_id, .order_in_region = 0 },
				instr->select.value);
		break;
	}
	case INSTR_COUNT:
		unreachable();
	}

	profile_scope_end();
}

static int _compare_scheduled_instr(void* context_ptr, const void* a_ptr, const void* b_ptr) {
	InstrIndex a = *(InstrIndex*)a_ptr;
	InstrIndex b = *(InstrIndex*)b_ptr;

	const InstrPosition* positions = (const InstrPosition*)context_ptr;
	uint16_t order_a = positions[a.value].order_in_region;
	uint16_t order_b = positions[b.value].order_in_region;

	if (order_a == order_b) {
		return 0;
	}

	return order_a < order_b ? 1 : -1;
}

// Sorts instructions in descending order based on `order_in_region` stored in
// `InstrSchedulingState`
static void _sort_scheduled_instr(InstrIndexArray instr_array, InstrPosition* positions) {
	profile_scope_start(__func__);

	qsort_s(instr_array.instr,
			instr_array.count,
			sizeof(*instr_array.instr),
			_compare_scheduled_instr,
			positions);

	profile_scope_end();
}

typedef struct {
	// For each instruction in `instr_buffer` stores the `region_id` of the region, where this
	// instruction is scheduled.
	uint16_t* instr_scheduled_region;

	// Accompanies the above array providing the index of that instruction in the scheduled region.
	uint16_t* instr_position_in_region;

	// All scheduled instructions for all regions, layed out as a liniar array.
	InstrIndexArray all_instr;

	// Per region array of instructions.
	//
	// Each per region array is a view into `all_instr`
	InstrIndexArray* scheduled_instr;
} SchedulingResult;

static void _schedule_instr(const InstrBuffer* instr_buffer,
		InstrIndexArray scheduled_regions,
		const CFGDominatorTree* dom_tree,
		SchedulingResult* out_result,
		Arena* allocator,
		Arena* temp_allocator) {
	profile_scope_start(__func__);
	ArenaRegion temp = arena_begin_temp(temp_allocator);

	InstrPosition* positions = arena_alloc_array(temp_allocator,
			InstrPosition,
			instr_buffer->count);
	memset(positions, 0xff, sizeof(*positions) * instr_buffer->count);

	uint16_t* next_position_in_region = arena_alloc_array_zeroed(temp_allocator,
			uint16_t,
			instr_buffer->region_count);

	InstrSchedulingContext context;
	context.instr_buffer = instr_buffer;
	context.positions = positions;
	context.dom_tree = dom_tree;
	context.temp_allocator = temp_allocator;
	context.next_position_in_region = next_position_in_region;

	// Assign the lowest order number to each control instruction, so that they appear right at the
	// end of the corresponding region.
	for (size_t i = 0; i < scheduled_regions.count; i += 1) {
		InstrIndex root_region_index = scheduled_regions.instr[i];
		const Instr* root_region = instr_buffer_at(instr_buffer, root_region_index);

		uint16_t current_region_id = root_region->region.id;

		uint16_t order_in_region = next_position_in_region[current_region_id];
		next_position_in_region[current_region_id] += 1;

		positions[root_region->region.last_instr.value].region_id = current_region_id;
		positions[root_region->region.last_instr.value].order_in_region = order_in_region;
	}

	{
		ArenaRegion temp1 = arena_begin_temp(temp_allocator);

		InstrQueue queue;
		instr_queue_alloc(&queue, temp_allocator, instr_buffer->count);

		for (size_t i = 0; i < scheduled_regions.count; i += 1) {
			InstrIndex root_region_index = scheduled_regions.instr[i];
			const Instr* root_region = instr_buffer_at(instr_buffer, root_region_index);

			instr_queue_push_back(&queue, root_region->region.last_instr);

			while (queue.count) {
				InstrIndex instr_index = instr_queue_pop_front(&queue);
				_enqueue_inputs_for_scheduling(&queue, instr_index, &context);
			}
		}

		arena_end_temp(temp1);
	}

	// Prepare all the necessary buffers to store the scheduling results.
	uint16_t* instr_count_per_region = arena_alloc_array_zeroed(temp_allocator,
			uint16_t,
			instr_buffer->region_count);

	for (uint16_t i = 0; i < instr_buffer->count; i += 1) {
		if (positions[i].region_id == UINT16_MAX) {
			// This instruction doesn't belong to any of the regions.
			continue;
		}

		instr_count_per_region[positions[i].region_id] += 1;
	}

	InstrIndexArray* scheduled_instr_per_region = arena_alloc_array(allocator,
			InstrIndexArray,
			instr_buffer->region_count);

	InstrIndexArray all_instr = {};
	all_instr.instr = arena_alloc_array(allocator, InstrIndex, 0);

	for (uint16_t i = 0; i < instr_buffer->region_count; i += 1) {
		scheduled_instr_per_region[i].instr = arena_alloc_array(allocator,
				InstrIndex,
				instr_count_per_region[i]);
		scheduled_instr_per_region[i].count = 0;

		all_instr.count += instr_count_per_region[i];
	}

	uint16_t* instr_scheduled_region = arena_alloc_array(allocator,
			uint16_t,
			instr_buffer->count);
	uint16_t* instr_position_in_region = arena_alloc_array(allocator,
			uint16_t,
			instr_buffer->count);

	memset(instr_scheduled_region, 0xff, sizeof(*instr_scheduled_region) * instr_buffer->count);
	memset(instr_position_in_region, 0xff, sizeof(*instr_position_in_region) * instr_buffer->count);

	// Now append each instruction to the corresponding region.
	for (uint16_t i = 0; i < instr_buffer->count; i += 1) {
		if (positions[i].region_id == UINT16_MAX) {
			continue;
		}

		const Instr* instr = &instr_buffer->instr[i];

		uint16_t region_id = positions[i].region_id;
		InstrIndexArray* region_instr_array = &scheduled_instr_per_region[region_id];

		instr_scheduled_region[i] = region_id;

		region_instr_array->instr[region_instr_array->count] = (InstrIndex) { i };
		region_instr_array->count += 1;
	}

	// During scheduling each instruction get assigned an order number, upon being added to a
	// specific region.
	//
	// Lower order number means - the instruction must appear later in the region.
	//
	// Since in the above loop the instructions were added in the same order they appear in the
	// `instr_buffer`, it means they don't necessarily follow the order they were assigned during
	// scheduling.
	//
	// To correct this, and satisfy the order rules, just sort all the instruction of each regions,
	// based on that order number.
	for (uint16_t i = 0; i < scheduled_regions.count; i += 1) {
		uint32_t region_id = instr_region_id(instr_buffer, scheduled_regions.instr[i]);

		InstrIndexArray region_instr_array = scheduled_instr_per_region[region_id];
		_sort_scheduled_instr(region_instr_array, positions);

		for (size_t j = 0; j < region_instr_array.count; j += 1) {
			InstrIndex instr = region_instr_array.instr[j];

			instr_position_in_region[instr.value] = (uint16_t)j;
		}
	}

	arena_end_temp(temp);

	out_result->instr_scheduled_region = instr_scheduled_region;
	out_result->all_instr = all_instr;
	out_result->scheduled_instr = scheduled_instr_per_region;
	out_result->instr_position_in_region = instr_position_in_region;

	profile_scope_end();
}

static void _schedule_regions(const InstrBuffer* instr_buffer,
		InstrIndex region_instr_index,
		Arena* allocator,
		BitArray* visited_regions,
		InstrIndexArray* out_scheduled) {
	profile_scope_start(__func__);

	const Instr* instr = instr_buffer_at(instr_buffer, region_instr_index);
	assert(instr->kind == INSTR_REGION);

	uint16_t region_id = instr->region.id;
	if (bit_array_get(visited_regions, region_id)) {
		profile_scope_end();
		return;
	}

	bit_array_set(visited_regions, region_id, true);

	const Instr* last_instr = instr_buffer_at(instr_buffer, instr->region.last_instr);
	switch (last_instr->kind) {
	case INSTR_JUMP:
		_schedule_regions(instr_buffer,
				last_instr->jump.target_region,
				allocator,
				visited_regions,
				out_scheduled);
		break;
	case INSTR_BRANCH:
		_schedule_regions(instr_buffer,
				last_instr->branch.false_region,
				allocator,
				visited_regions,
				out_scheduled);
		_schedule_regions(instr_buffer,
				last_instr->branch.true_region,
				allocator,
				visited_regions,
				out_scheduled);
		break;
	case INSTR_RET:
	case INSTR_RETURN_VALUE:
		break;
	default:
		unreachable();
	}

	*arena_alloc(allocator, InstrIndex) = region_instr_index;
	out_scheduled->count += 1;

	profile_scope_end();
}

static InstrIndexArray _gather_scheduled_regions(X64CodeGenerator* gen, InstrIndex initial_region) {
	profile_scope_start(__func__);
	
	BitArray visited_regions = bit_array_alloc(gen->temp_allocator, gen->instr_buffer.region_count);
	bit_array_clear(&visited_regions);

	InstrIndexArray regions;
	regions.instr = arena_alloc_array(gen->temp_allocator, InstrIndex, 0);
	regions.count = 0;

	_schedule_regions(&gen->instr_buffer,
			initial_region,
			gen->temp_allocator,
			&visited_regions,
			&regions);

	for (size_t i = 0; i < regions.count / 2; i += 1) {
		size_t j = regions.count - 1 - i;

		InstrIndex temp = regions.instr[i];
		regions.instr[i] = regions.instr[j];
		regions.instr[j] = temp;
	}

	profile_scope_end();
	return regions;
}

LoweredFunction x64_generate_code(X64CodeGenerator* gen, InstrIndex root_region) {
	profile_scope_start(__func__);

	encoding_init();
	ArenaRegion temp = arena_begin_temp(gen->temp_allocator);

	_merge_string_consts(gen);

	InstrIndexArray scheduled_regions = _gather_scheduled_regions(gen, root_region);

	CFGDominatorTree dom_tree = dom_tree_build(&gen->instr_buffer,
			root_region,
			gen->allocator,
			gen->temp_allocator);

	CFGDominatorTree post_dom_tree = post_dom_tree_build(&gen->instr_buffer,
			scheduled_regions,
			gen->allocator,
			gen->temp_allocator);

	if (has_flag(gen->flags, X64_DEBUG_LOG)) {
		printf("dominator tree:\n");
		dom_tree_print(&dom_tree, &gen->instr_buffer);
		printf("post-dominator tree:\n");
		dom_tree_print(&post_dom_tree, &gen->instr_buffer);
	}

	_collect_phis(gen, gen->temp_allocator);

	SchedulingResult scheduling_result = {};
	_schedule_instr(&gen->instr_buffer,
			scheduled_regions,
			&dom_tree,
			&scheduling_result,
			gen->temp_allocator,
			gen->allocator);

	gen->live_ranges = instr_compute_live_ranges(gen->instr_buffer,
			root_region,
			scheduled_regions,
			scheduling_result.scheduled_instr,
			// use `temp_allocator` as a persistent, since live ranges aren't needed outside this
			// function
			&dom_tree,
			gen->temp_allocator, 
			gen->allocator);

	// Print scheduled instructions
	if (has_flag(gen->flags, X64_PRINT_SCHEDULED_IR)) {
		const InstrBuffer* instr_buffer = &gen->instr_buffer;

		size_t instr_offset = 0;
		for (size_t i = 0; i < scheduled_regions.count; i += 1) {
			InstrIndex region_instr = scheduled_regions.instr[i];
			const Instr* instr = instr_buffer_at(instr_buffer, region_instr);

			InstrIndexArray scheduled = scheduling_result.scheduled_instr[instr->region.id];

			printf("region %%%u id=%u: \n", (uint32_t)region_instr.value, (uint32_t)instr->region.id);
			for (size_t j = 0; j < scheduled.count; j++) {
				ArenaRegion temp = arena_begin_temp(gen->temp_allocator);
				InstrIndex instr_index = scheduled.instr[j];

				InstrLiveRange live_range = gen->live_ranges[instr_index.value];

				printf("%zu\033[12G[%u; %u]\033[24G%%%u:\033[30G",
						instr_offset,
						live_range.start,
						live_range.end,
						(uint32_t)instr_index.value);

				instr_print(&gen->instr_buffer.instr[instr_index.value],
						gen->instr_buffer.inputs_buffer,
						gen->temp_allocator);

				arena_end_temp(temp);

				instr_offset += 1;
			}
			printf("\n");
		}
	}

	// Now check that scheduling is valid
	bool scheduling_is_valid = true;
	for (size_t i = 0; i < scheduled_regions.count; i += 1) {
		InstrIndex region_instr = scheduled_regions.instr[i];
		const Instr* instr = &gen->instr_buffer.instr[region_instr.value];

		InstrIndexArray scheduled = scheduling_result.scheduled_instr[instr->region.id];
		scheduling_is_valid &= _validate_instr_scheduling_for_region(&gen->instr_buffer,
				scheduling_result.instr_scheduled_region,
				scheduling_result.instr_position_in_region,
				instr->region.id,
				scheduled,
				&dom_tree,
				gen->temp_allocator);
	}

	assert_msg(scheduling_is_valid, "Instruction scheduler failed to produce a valid result");

	if (!has_flag(gen->flags, X64_SKIP_REG_ALLOC)) {
		_run_reg_allocator(gen, scheduling_result.all_instr);
	}

	gen->call_addr_placeholders = NULL;
	gen->call_addr_placeholder_count = 0;
	gen->call_addr_placeholder_capacity = 0;

	for (size_t i = 0; i < scheduled_regions.count; i += 1) {
		const InstrBuffer* instr_buffer = &gen->instr_buffer;

		InstrIndex region_instr = scheduled_regions.instr[i];
		const Instr* instr = &gen->instr_buffer.instr[region_instr.value];

		InstrIndexArray scheduled = scheduling_result.scheduled_instr[instr->region.id];
		for (size_t j = 0; j < scheduled.count; j++) {
			InstrIndex instr_index = scheduled.instr[j];
			const Instr* instr = instr_buffer_at(instr_buffer, instr_index);

			if (instr->kind == INSTR_CALL_INDIRECT
					|| instr->kind == INSTR_CALL_DIRECT
					|| instr->kind == INSTR_LOAD_FUNCTION_ADDR
					|| instr->kind == INSTR_LOAD_EXTERNAL_FUNCTION_ADDR) {
				gen->call_addr_placeholder_capacity += 1;
			}
		}
	}

	gen->call_addr_placeholders = arena_alloc_array(gen->allocator,
			CallAddressPlaceholder,
			gen->call_addr_placeholder_capacity);

	gen->call_addr_placeholder_regions = arena_alloc_array(gen->temp_allocator,
			uint16_t,
			gen->call_addr_placeholder_capacity);

	uint16_t region_count = gen->instr_buffer.region_count;
	gen->per_region_code_buffer = arena_alloc_array_zeroed(gen->temp_allocator,
			CodeBuffer,
			region_count);

	for (size_t i = 0; i < scheduled_regions.count; i += 1) {
		InstrIndex region_instr = scheduled_regions.instr[i];
		const Instr* instr = &gen->instr_buffer.instr[region_instr.value];

		CodeBuffer* code_buffer = &gen->per_region_code_buffer[instr->region.id];
		code_buffer_init(code_buffer, gen->temp_allocator);

		if (region_instr.value == root_region.value) {
			_emit_callee_prologue(gen, code_buffer);
		}

		InstrIndexArray scheduled = scheduling_result.scheduled_instr[instr->region.id];
		for (size_t j = 0; j < scheduled.count; j += 1) {
			_lower_instr(gen, scheduled.instr[j], instr->region.id, code_buffer);
		}
	}

	size_t final_code_size = 0;
	size_t* code_block_offsets = arena_alloc_array(gen->temp_allocator, size_t, region_count);
	size_t* control_instr_size = arena_alloc_array(gen->temp_allocator, size_t, region_count);
	for (uint16_t i = 0; i < scheduled_regions.count; i += 1) {
		InstrBuffer* instr_buffer = &gen->instr_buffer;

		const Instr* region_instr = instr_buffer_at(instr_buffer, scheduled_regions.instr[i]);
		uint16_t region_id = instr_region_id(&gen->instr_buffer, scheduled_regions.instr[i]);

		const CodeBuffer* code_buffer = &gen->per_region_code_buffer[region_id];
		code_block_offsets[region_id] = final_code_size;
		control_instr_size[region_id] = _compute_control_instr_encoding_size(
				instr_buffer_at(instr_buffer, region_instr->region.last_instr));

		final_code_size += code_buffer->size;
		final_code_size += control_instr_size[region_id];
	}

	// Correct call address placeholders, now that we have a clear idea of where each block will be
	// located in the memory.
	//
	// When these placeholder were just being created, all the offsets were relative to the block.
	// However, in order to compute the relative offsets for the call instructions, we need these
	// offsets to be relative to the whole program.
	for (size_t i = 0; i < gen->call_addr_placeholder_count; i += 1) {
		uint16_t placeholder_region_id = gen->call_addr_placeholder_regions[i];

		size_t block_offset = code_block_offsets[placeholder_region_id];
		gen->call_addr_placeholders[i].instruction_end_offset += block_offset;
		gen->call_addr_placeholders[i].addr_offset += block_offset;
	}

	uint8_t* final_code = arena_alloc_aligned(gen->allocator, final_code_size, 16);
	for (uint16_t i = 0; i < scheduled_regions.count; i += 1) {
		InstrBuffer* instr_buffer = &gen->instr_buffer;

		const Instr* region_instr = instr_buffer_at(instr_buffer, scheduled_regions.instr[i]);
		uint16_t region_id = instr_region_id(&gen->instr_buffer, scheduled_regions.instr[i]);

		size_t block_size = gen->per_region_code_buffer[region_id].size;
		size_t block_offset = code_block_offsets[region_id];

		memcpy(final_code + block_offset,
				gen->per_region_code_buffer[region_id].buffer,
				block_size);

		CodeBuffer control_instr_buffer = {};
		code_buffer_wrap(&control_instr_buffer,
				final_code + block_offset + block_size,
				control_instr_size[region_id]);

		_encode_control_instr(
				instr_buffer_at(instr_buffer, region_instr->region.last_instr),
				instr_buffer,
				block_offset + block_size,
				code_block_offsets,
				&control_instr_buffer);

		assert(control_instr_buffer.size == control_instr_buffer.capacity);
	}

	const InstrBuffer* instr_buffer = &gen->instr_buffer;
	size_t entry_point_offset = code_block_offsets[instr_region_id(instr_buffer, root_region)];
	assert(entry_point_offset == 0);

	LoweredFunction machine_code;
	machine_code.code = final_code;
	machine_code.size_in_bytes = final_code_size;
	machine_code.call_addr_placeholder_count = gen->call_addr_placeholder_count;
	machine_code.call_addr_placeholders = gen->call_addr_placeholders;

	arena_end_temp(temp);
	profile_scope_end();
	return machine_code;
}

CallFrameLayout compute_call_frame_layout(const AbiSignature* signature, Arena* allocator) {
	profile_scope_start(__func__);

	InstrStorageLocation* locations = arena_alloc_array(allocator,
			InstrStorageLocation,
			signature->param_count);

	uint32_t stack_slot = 0;
	size_t arg_reg_index = 0;

	for (uint32_t i = 0; i < signature->param_count; i += 1) {
		AbiParam param = signature->params[i];

		switch (param.kind) {
		case ABI_PARAM_NORMAL:
		case ABI_PARAM_STRUCT:
			if (arg_reg_index < array_size(CDECL_ARG_REGS)) {
				locations[i].kind = INSTR_STORAGE_REG;
				locations[i].reg = CDECL_ARG_REGS[arg_reg_index];

				arg_reg_index += 1;
			} else {
				locations[i].kind = INSTR_STORAGE_CALL_FRAME;
				locations[i].call_frame.slot = stack_slot;

				stack_slot += 1;
			}

			break;
		case ABI_PARAM_RETURN_LOCATION:
			assert(arg_reg_index < array_size(CDECL_ARG_REGS));

			locations[i].kind = INSTR_STORAGE_REG;
			locations[i].reg = CDECL_ARG_REGS[arg_reg_index];

			arg_reg_index += 1;
			break;
		}
	}

	profile_scope_end();
	return (CallFrameLayout) {
		.locations = locations,
		.location_count = signature->param_count,
		.stack_slot_count = stack_slot,
	};
}

//
// Internal
// 

typedef struct {
	X64Register* regs;
	size_t count;
} RegisterArray;

RegisterMoveArray _parallel_move_values(
		const InstrStorageLocation* input_locations,
		const X64Register* target_locations,
		size_t location_count,
		uint16_t allowed_temp_registers,
		Arena* allocator,
		Arena* temp_allocator) {
	profile_scope_start(__func__);

	// Validate that none of the target and inputs locations overlap with temp registers
	//
	// In case we have a cycle, we need to save one of the registers to a temporary, same way when
	// we need to swap values of two variables:
	//
	// temp = a
	// a = b
	// b = temp
	//
	// However if the bit mask of allowed temporary registers contains the ones that are assigned to
	// `target_locations` or `input_locations`, saving to a temporary register might override one
	// of the values we're trying to parallel move into target locations.
	for (size_t i = 0; i < location_count; i += 1) {
		assert_msg(!has_flag(allowed_temp_registers, 1 << target_locations[i]),
				"Target location overlaps with temporary registers");

		assert(input_locations[i].kind == INSTR_STORAGE_REG);
		assert_msg(!has_flag(allowed_temp_registers, 1 << input_locations[i].reg),
				"Input location overlaps with temporary registers");
	}

	{
		uint16_t target_reg_mask = 0;
		for (size_t i = 0; i < location_count; i += 1) {
			uint16_t reg_mask = 1 << target_locations[i];
			assert_msg((target_reg_mask & reg_mask) == 0, "Duplicate target locations");

			target_reg_mask |= reg_mask;
		}
	}

	ArenaRegion temp = arena_begin_temp(temp_allocator);

	const X64Register INVALID_REGISTER = -1;
	X64Register map[X64_REG_COUNT];
	memset(map, 0xff, sizeof(map));

	for (uint16_t i = 0; i < location_count; i += 1) {
		const InstrStorageLocation input_location = input_locations[i];
		assert(input_location.kind == INSTR_STORAGE_REG);

		map[target_locations[i]] = input_location.reg;
	}

	BitArray is_move_target = bit_array_alloc(temp_allocator, X64_REG_COUNT);
	bit_array_clear(&is_move_target);

	for (size_t i = 0; i < X64_REG_COUNT; i += 1) {
		if (map[i] != INVALID_REGISTER) {
			bit_array_set(&is_move_target, map[i], true);
		}
	}

	BitArray resolved_slots = bit_array_alloc(temp_allocator, X64_REG_COUNT);
	bit_array_clear(&resolved_slots);

	BitArray visited = bit_array_alloc(temp_allocator, X64_REG_COUNT);

	RegisterMoveArray result;
	result.moves = arena_alloc_array(allocator, RegisterMove, 0);
	result.count = 0;

	for (size_t reg_index = 0; reg_index < location_count; reg_index += 1) {
		X64Register reg = target_locations[reg_index];
		if (bit_array_get(&resolved_slots, reg)) {
			continue;
		}

		if (bit_array_get(&is_move_target, reg)) {
			continue;
		}

		bit_array_clear(&visited);
		X64Register current_reg = reg;
		RegisterArray move_path;
		move_path.regs = arena_alloc_array(temp_allocator, X64Register, 0);
		move_path.count = 0;

		while (true) {
			assert_msg(bit_array_get(&visited, current_reg) == false, "Expected no cycles");

			bit_array_set(&visited, current_reg, true);
			bit_array_set(&resolved_slots, current_reg, true);

			arena_alloc(temp_allocator, X64Register);
			move_path.regs[move_path.count] = current_reg;
			move_path.count += 1;

			if (map[current_reg] == INVALID_REGISTER) {
				// No edge -> stop
				break;
			}

			current_reg = map[current_reg];
		}

		for (size_t i = 0; i + 1 < move_path.count; i += 1) {
			X64Register src = move_path.regs[i + 1];
			X64Register dst = move_path.regs[i];

			if (src == dst) {
				continue;
			}

			arena_alloc(allocator, RegisterMove);
			result.moves[result.count] = (RegisterMove) { .src = src, .dst = dst, };
			result.count += 1;
		}
	}

	for (size_t reg_index = 0; reg_index < location_count; reg_index += 1) {
		X64Register reg = target_locations[reg_index];
		if (bit_array_get(&resolved_slots, reg)) {
			continue;
		}

		bit_array_clear(&visited);
		X64Register current_reg = reg;
		RegisterArray move_path;
		move_path.regs = arena_alloc_array(temp_allocator, X64Register, 0);
		move_path.count = 0;

		while (true) {
			if (bit_array_get(&visited, current_reg)) {
				// found a cycle -> stop
				break;
			}

			assert(bit_array_get(&resolved_slots, current_reg) == false);

			bit_array_set(&visited, current_reg, true);
			bit_array_set(&resolved_slots, current_reg, true);

			arena_alloc(temp_allocator, X64Register);
			move_path.regs[move_path.count] = current_reg;
			move_path.count += 1;

			if (map[current_reg] == INVALID_REGISTER) {
				// No edge -> stop
				break;
			}

			current_reg = map[current_reg];
		}

		if (move_path.count == 1) {
			// The input is already at the expected location
			continue;
		}

		assert_msg(allowed_temp_registers != 0, "Found a cycle, but there are no available temp"
				" registers to save one of the registers in the cycle");

		X64Register temp_save_register = count_trailing_zeros(allowed_temp_registers);

		arena_alloc(allocator, RegisterMove);
		result.moves[result.count] = (RegisterMove) {
			.src = move_path.regs[0],
			.dst = temp_save_register,
		};
		result.count += 1;

		for (size_t i = 0; i + 1 < move_path.count; i += 1) {
			X64Register src = move_path.regs[i + 1];
			X64Register dst = move_path.regs[i];

			if (src == dst) {
				continue;
			}

			arena_alloc(allocator, RegisterMove);
			result.moves[result.count] = (RegisterMove) { .src = src, .dst = dst, };
			result.count += 1;
		}

		arena_alloc(allocator, RegisterMove);
		result.moves[result.count] = (RegisterMove) {
			.src = temp_save_register,
			.dst = move_path.regs[move_path.count - 1],
		};
		result.count += 1;
	}

	for (size_t i = 0; i < location_count; i += 1) {
		assert(bit_array_get(&resolved_slots, target_locations[i]));
	}

	arena_end_temp(temp);
	profile_scope_end();
	return result;
}

