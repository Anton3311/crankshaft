#ifndef X64_ENCODING_H
#define X64_ENCODING_H

#include "core/core.h"

#define X64_GPR_COUNT 16

//
// CodeBuffer
//

typedef struct {
	uint8_t* buffer;
	size_t size;
	size_t capacity;
	Arena* allocator;

	size_t instruction_count;
} CodeBuffer;

void code_buffer_init(CodeBuffer* buffer, Arena* allocator);
void code_buffer_wrap(CodeBuffer* buffer, uint8_t* backing_buffer, size_t backing_buffer_size);
void code_buffer_grow(CodeBuffer* buffer, size_t expected_capacity);

inline uint8_t* code_buffer_append(CodeBuffer* buffer, size_t byte_count) {
	if (buffer->size + byte_count > buffer->capacity) {
		code_buffer_grow(buffer, buffer->size + byte_count);
	}

	uint8_t* bytes = buffer->buffer + buffer->size;
	buffer->size += byte_count;
	return bytes;
}

//
// Encoding
//

typedef struct Operand Operand;

typedef enum {
	MNEMONIC_ADD,
	MNEMONIC_OR,
	MNEMONIC_AND,
	MNEMONIC_SUB,
	MNEMONIC_XOR,

	MNEMONIC_CMP,

	MNEMONIC_PUSH,
	MNEMONIC_POP,

	MNEMONIC_JO,
	MNEMONIC_JNO,
	MNEMONIC_JB,
	MNEMONIC_JNB,
	MNEMONIC_JZ,
	MNEMONIC_JNZ,
	MNEMONIC_JBE,
	MNEMONIC_JNBE,
	MNEMONIC_JS,
	MNEMONIC_JNS,
	MNEMONIC_JP,
	MNEMONIC_JNP,
	MNEMONIC_JL,
	MNEMONIC_JNL,
	MNEMONIC_JLE,
	MNEMONIC_JNLE,

	MNEMONIC_TEST,

	MNEMONIC_SETZ,
	MNEMONIC_SETNZ,
	MNEMONIC_SETBE,
	MNEMONIC_SETNBE,
	MNEMONIC_SETS,
	MNEMONIC_SETNS,
	MNEMONIC_SETP,
	MNEMONIC_SETNP,
	MNEMONIC_SETL,
	MNEMONIC_SETNL,
	MNEMONIC_SETLE,
	MNEMONIC_SETNLE,

	MNEMONIC_MOV,
	MNEMONIC_LEA,

	MNEMONIC_INT3,

	// To encode mul/imul ax, al, r/m - use `encode_1` with `r/m` operand.
	// No need to pass the first two register operands
	MNEMONIC_MUL,
	MNEMONIC_IMUL,
	MNEMONIC_DIV,
	MNEMONIC_IDIV,

	MNEMONIC_MOVZX,
	MNEMONIC_MOVSX,
	MNEMONIC_MOVSXD,

	// Copy sign bit of `AX` into every bit of `DX`
	MNEMONIC_CWD,
	// Copy sign bit of `EAX` into every bit of `EDX`
	MNEMONIC_CDQ,
	// Copy sign bit of `RAX` into every bit of `RDX`
	MNEMONIC_CQO,

	// shr r/m8,        imm8
	// shr r/m16/32/64, imm8
	// shr r/m8,        CL
	// shr r/m16/32/64, CL
	MNEMONIC_SHR,
	// shl r/m8,        imm8
	// shl r/m16/32/64, imm8
	// shl r/m8,        CL
	// shl r/m16/32/64, CL
	MNEMONIC_SHL,

	MNEMONIC_RET,

	MNEMONIC_JMP,

	MNEMONIC_CALL,

	MNEMONIC_NOT,
	MNEMONIC_NEG,

	MNEMONIC_COUNT,
} MnemonicKind;

enum OperandKind {
	OP_NONE  = 0,
	OP_REG   = 1 << 0,
	OP_IMM   = 1 << 1,
	OP_MEM   = 1 << 2,
	OP_REL   = 1 << 3,
};

typedef uint8_t OperandKind;

struct Operand {
	OperandKind kind;
	uint8_t bit_count;

	union {
		struct {
			bool is_rip_relative;
		} mem;
	} extra;

	union {
		uint8_t reg;
		struct {
			uint8_t base_reg;
			int32_t disp;
		} mem;
		uint64_t imm;
		int32_t rel;
	};
};

inline Operand operand_none() { return (Operand) {}; }

inline Operand operand_reg(uint8_t reg_index, uint8_t bit_count) {
	Operand op = {};
	op.kind = OP_REG;
	op.reg = reg_index;
	op.bit_count = bit_count;
	return op;
}

// NOTE: Here `bit_count` referes to the size of the value stored at the given address.
//       The provided address in turn is stored in the register at `reg_index`
inline Operand operand_mem(uint8_t reg_index, uint8_t bit_count) {
	Operand op = {};
	op.kind = OP_MEM;
	op.mem.base_reg = reg_index;
	op.mem.disp = 0;
	op.bit_count = bit_count;
	return op;
}

inline Operand operand_stack_mem(int32_t offset, uint8_t bit_count) {
	Operand op = {};
	op.kind = OP_MEM;
	op.mem.base_reg = 4 /* X64_REG_SP */;
	op.mem.disp = offset;
	op.bit_count = bit_count;
	return op;
}

inline Operand operand_imm(uint64_t imm, uint8_t bit_count) {
	Operand op = {};
	op.kind = OP_IMM;
	op.imm = imm;
	op.bit_count = bit_count;
	return op;
}

inline Operand operand_rel32(int32_t offset) {
	Operand op = {};
	op.kind = OP_REL;
	op.rel = offset;
	op.bit_count = 32;
	return op;
}

inline Operand operand_rip_relative(int32_t offset, uint8_t bit_count) {
	Operand op = {};
	op.kind = OP_MEM;
	op.extra.mem.is_rip_relative = true;
	op.mem.base_reg = 13 /* rip */;
	op.mem.disp = offset;
	op.bit_count = bit_count;
	return op;
}

// Initialize look up tables need to accelerate encoding
void encoding_init();

#define MAX_ENCODING_SIZE 16

void encode_n(CodeBuffer* code_buffer,
		MnemonicKind mnemonic,
		const Operand* operands,
		uint8_t operand_count);

inline void encode_1(CodeBuffer* code_buffer,
		MnemonicKind mnemonic,
		Operand op0) {
	Operand operands[] = { op0 };
	encode_n(code_buffer, mnemonic, operands, 1);
}

inline void encode_2(CodeBuffer* code_buffer,
		MnemonicKind mnemonic,
		Operand op0,
		Operand op1) {
	Operand operands[] = { op0, op1 };
	encode_n(code_buffer, mnemonic, operands, 2);
}

inline size_t compute_encoding_size(MnemonicKind mnemonic,
		const Operand* operands,
		uint8_t operand_count) {
	uint8_t backing_buffer[MAX_ENCODING_SIZE];
	CodeBuffer buffer;
	code_buffer_wrap(&buffer, backing_buffer, sizeof(backing_buffer));
	encode_n(&buffer, mnemonic, operands, operand_count);
	return buffer.size;
}

inline size_t compute_encoding_size_1(MnemonicKind mnemonic, Operand op0) {
	Operand operands[] = { op0 };

	uint8_t backing_buffer[MAX_ENCODING_SIZE];
	CodeBuffer buffer;
	code_buffer_wrap(&buffer, backing_buffer, sizeof(backing_buffer));
	encode_n(&buffer, mnemonic, operands, 1);

	return buffer.size;
}

inline size_t compute_encoding_size_2(MnemonicKind mnemonic, Operand op0, Operand op1) {
	Operand operands[] = { op0, op1 };

	uint8_t backing_buffer[MAX_ENCODING_SIZE];
	CodeBuffer buffer;
	code_buffer_wrap(&buffer, backing_buffer, sizeof(backing_buffer));
	encode_n(&buffer, mnemonic, operands, 2);

	return buffer.size;
}

#endif
