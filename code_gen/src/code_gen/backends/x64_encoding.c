#include "x64_encoding.h"

//
// CodeBuffer
//

#define CODE_BUFFER_ALLOCATION_STEP 64
#define CODE_BUFFER_ALLOCATION_STEP_MASK (CODE_BUFFER_ALLOCATION_STEP - 1)

void code_buffer_init(CodeBuffer* buffer, Arena* allocator) {
	buffer->allocator = allocator;
	buffer->capacity = 0;
	buffer->size = 0;
	buffer->buffer = arena_alloc_array(allocator, uint8_t, 0);
}

void code_buffer_wrap(CodeBuffer* buffer, uint8_t* backing_buffer, size_t backing_buffer_size) {
	buffer->allocator = NULL;
	buffer->size = 0;
	buffer->capacity = backing_buffer_size;
	buffer->buffer = backing_buffer;
}

void code_buffer_grow(CodeBuffer* buffer, size_t expected_capacity) {
	assert(buffer->allocator);

	size_t capacity_delta = expected_capacity - buffer->capacity;
	size_t allocation_size = (capacity_delta + CODE_BUFFER_ALLOCATION_STEP - 1) & ~CODE_BUFFER_ALLOCATION_STEP_MASK;

	assert_msg(buffer->buffer + buffer->capacity == buffer->allocator->base + buffer->allocator->allocated,
			"Trying to grow CodeBuffer, however since the last grow there were allocations done, "
			"with the arena associated with this code buffer");

	arena_alloc_array(buffer->allocator, uint8_t, allocation_size);

	buffer->capacity += allocation_size;
}

//
// Encoding
//

enum EncodingFlags {
	ENC_NONE               = 0,
	ENC_HAS_0F_PREFIX      = 1 << 0,

	// The destination reg index is added to the opcode byte. ModRM not used.
	ENC_ADD_REG_TO_OPCODE  = 1 << 1,

	// The REX prefix with (W == 1) must be added no matter what the operands are.
	ENC_MANDATORY_REX_64    = 1 << 2,
	// THe operand size override prefix must be added no matter what the operands are.
	ENC_MANDATORY_66       = 1 << 3
};

typedef uint8_t EncodingFlags;

enum ModRM {
	MOD_RM_ADDRESS_RM         = 0b00000000,
	MOD_RM_ADDRESS_RM_DISP_8  = 0b01000000,
	MOD_RM_ADDRESS_RM_DISP_32 = 0b10000000,
	MOD_RM_RM                 = 0b11000000,
};

#define MAX_OPERAND_COUNT 4

typedef uint8_t ModRM;

typedef struct {
	OperandKind kind;
	uint8_t sizes;
} OperandEncoding;

typedef struct {
	MnemonicKind mnemonic;
	EncodingFlags flags;
	uint8_t opcode;
	uint8_t mod_rm_ext;

	OperandEncoding operands[MAX_OPERAND_COUNT];
} Encoding;

typedef struct {
	size_t start;
	size_t end;
} EncodingRange;

typedef struct {
	uint8_t operand_count;
	uint8_t reg_operand_count;
	bool has_mod_rm;
} EncodingExtra;

#define OP_RM OP_REG | OP_MEM

static Encoding s_encodings[118];

static EncodingRange s_encoding_ranges[MNEMONIC_COUNT];
static bool s_encoding_initialized = false;
static EncodingExtra s_encoding_extra[array_size(s_encodings)];

void encoding_init() {
	if (s_encoding_initialized) {
		return;
	}

	profile_scope_start(__func__);

	typedef Encoding E;
	Encoding* e = s_encodings;

	// add
	*(e++) = (E) { MNEMONIC_ADD, ENC_NONE, 0x00, 0x0, { { OP_RM,  8 },            { OP_REG, 8 } } };
	*(e++) = (E) { MNEMONIC_ADD, ENC_NONE, 0x01, 0x0, { { OP_RM,  16 | 32 | 64 }, { OP_REG, 16 | 32 | 64 } } };
	*(e++) = (E) { MNEMONIC_ADD, ENC_NONE, 0x02, 0x0, { { OP_REG, 8 },            { OP_RM,  8 } } };
	*(e++) = (E) { MNEMONIC_ADD, ENC_NONE, 0x03, 0x0, { { OP_REG, 16 | 32 | 64 }, { OP_RM,  16 | 32 | 64 } } };

	*(e++) = (E) { MNEMONIC_ADD, ENC_NONE, 0x80, 0x0, { { OP_RM, 8 },            { OP_IMM, 8 } } };
	*(e++) = (E) { MNEMONIC_ADD, ENC_NONE, 0x81, 0x0, { { OP_RM, 16 | 32 | 64 }, { OP_IMM, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_ADD, ENC_NONE, 0x83, 0x0, { { OP_RM, 16 | 32 | 64 }, { OP_IMM, 8 } } };

	// or
	*(e++) = (E) { MNEMONIC_OR, ENC_NONE, 0x08, 0x0, { { OP_RM,  8 },            { OP_REG, 8 } } };
	*(e++) = (E) { MNEMONIC_OR, ENC_NONE, 0x09, 0x0, { { OP_RM,  16 | 32 | 64 }, { OP_REG, 16 | 32 | 64 } } };
	*(e++) = (E) { MNEMONIC_OR, ENC_NONE, 0x0a, 0x0, { { OP_REG, 8 },            { OP_RM,  8 } } };
	*(e++) = (E) { MNEMONIC_OR, ENC_NONE, 0x0b, 0x0, { { OP_REG, 16 | 32 | 64 }, { OP_RM,  16 | 32 | 64 } } };

	*(e++) = (E) { MNEMONIC_OR, ENC_NONE, 0x80, 0x1, { { OP_RM, 8 },            { OP_IMM, 8 } } };
	*(e++) = (E) { MNEMONIC_OR, ENC_NONE, 0x81, 0x1, { { OP_RM, 16 | 32 | 64 }, { OP_IMM, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_OR, ENC_NONE, 0x83, 0x1, { { OP_RM, 16 | 32 | 64 }, { OP_IMM, 8 } } };

	// and
	*(e++) = (E) { MNEMONIC_AND, ENC_NONE, 0x20, 0x0, { { OP_RM,  8 },            { OP_REG, 8 } } };
	*(e++) = (E) { MNEMONIC_AND, ENC_NONE, 0x21, 0x0, { { OP_RM,  16 | 32 | 64 }, { OP_REG, 16 | 32 | 64 } } };
	*(e++) = (E) { MNEMONIC_AND, ENC_NONE, 0x22, 0x0, { { OP_REG, 8 },            { OP_RM,  8 } } };
	*(e++) = (E) { MNEMONIC_AND, ENC_NONE, 0x23, 0x0, { { OP_REG, 16 | 32 | 64 }, { OP_RM,  16 | 32 | 64 } } };

	*(e++) = (E) { MNEMONIC_AND, ENC_NONE, 0x80, 0x4, { { OP_RM, 8 },            { OP_IMM, 8 } } };
	*(e++) = (E) { MNEMONIC_AND, ENC_NONE, 0x81, 0x4, { { OP_RM, 16 | 32 | 64 }, { OP_IMM, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_AND, ENC_NONE, 0x83, 0x4, { { OP_RM, 16 | 32 | 64 }, { OP_IMM, 8 } } };

	// sub
	*(e++) = (E) { MNEMONIC_SUB, ENC_NONE, 0x28, 0x0, { { OP_RM,  8 },            { OP_REG, 8 } } };
	*(e++) = (E) { MNEMONIC_SUB, ENC_NONE, 0x29, 0x0, { { OP_RM,  16 | 32 | 64 }, { OP_REG, 16 | 32 | 64 } } };
	*(e++) = (E) { MNEMONIC_SUB, ENC_NONE, 0x2a, 0x0, { { OP_REG, 8 },            { OP_RM,  8 } } };
	*(e++) = (E) { MNEMONIC_SUB, ENC_NONE, 0x2b, 0x0, { { OP_REG, 16 | 32 | 64 }, { OP_RM,  16 | 32 | 64 } } };

	*(e++) = (E) { MNEMONIC_SUB, ENC_NONE, 0x80, 0x5, { { OP_RM, 8 },            { OP_IMM, 8 } } };
	*(e++) = (E) { MNEMONIC_SUB, ENC_NONE, 0x81, 0x5, { { OP_RM, 16 | 32 | 64 }, { OP_IMM, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_SUB, ENC_NONE, 0x83, 0x5, { { OP_RM, 16 | 32 | 64 }, { OP_IMM, 8 } } };

	// xor
	*(e++) = (E) { MNEMONIC_XOR, ENC_NONE, 0x30, 0x0, { { OP_RM,  8 },            { OP_REG, 8 } } };
	*(e++) = (E) { MNEMONIC_XOR, ENC_NONE, 0x31, 0x0, { { OP_RM,  16 | 32 | 64 }, { OP_REG, 16 | 32 | 64 } } };
	*(e++) = (E) { MNEMONIC_XOR, ENC_NONE, 0x32, 0x0, { { OP_REG, 8 },            { OP_RM,  8 } } };
	*(e++) = (E) { MNEMONIC_XOR, ENC_NONE, 0x33, 0x0, { { OP_REG, 16 | 32 | 64 }, { OP_RM,  16 | 32 | 64 } } };

	*(e++) = (E) { MNEMONIC_XOR, ENC_NONE, 0x80, 0x6, { { OP_RM, 8 },            { OP_IMM, 8 } } };
	*(e++) = (E) { MNEMONIC_XOR, ENC_NONE, 0x81, 0x6, { { OP_RM, 16 | 32 | 64 }, { OP_IMM, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_XOR, ENC_NONE, 0x83, 0x6, { { OP_RM, 16 | 32 | 64 }, { OP_IMM, 8 } } };

	// cmp
	*(e++) = (E) { MNEMONIC_CMP, ENC_NONE, 0x38, 0x0, { { OP_RM,  8 },            { OP_REG, 8 } } };
	*(e++) = (E) { MNEMONIC_CMP, ENC_NONE, 0x39, 0x0, { { OP_RM,  16 | 32 | 64 }, { OP_REG, 16 | 32 | 64 } } };
	*(e++) = (E) { MNEMONIC_CMP, ENC_NONE, 0x3a, 0x0, { { OP_REG, 8 },            { OP_RM,  8 } } };
	*(e++) = (E) { MNEMONIC_CMP, ENC_NONE, 0x3b, 0x0, { { OP_REG, 16 | 32 | 64 }, { OP_RM,  16 | 32 | 64 } } };

	*(e++) = (E) { MNEMONIC_CMP, ENC_NONE, 0x80, 0x7, { { OP_RM, 8 },            { OP_IMM, 8 } } };
	*(e++) = (E) { MNEMONIC_CMP, ENC_NONE, 0x81, 0x7, { { OP_RM, 16 | 32 | 64 }, { OP_IMM, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_CMP, ENC_NONE, 0x83, 0x7, { { OP_RM, 16 | 32 | 64 }, { OP_IMM, 8 } } };

	// push
	*(e++) = (E) { MNEMONIC_PUSH, ENC_ADD_REG_TO_OPCODE, 0x50, 0x0, { { OP_RM, 16 | 64 } } };

	// pop
	*(e++) = (E) { MNEMONIC_POP,  ENC_ADD_REG_TO_OPCODE, 0x58, 0x0, { { OP_RM, 16 | 64 } } };

	// j[0,b,nb,z,nz,be,nbe,s,ns,p,np,l,nl,le,nle]
	*(e++) = (E) { MNEMONIC_JO,   ENC_HAS_0F_PREFIX, 0x80, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JNO,  ENC_HAS_0F_PREFIX, 0x81, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JB,   ENC_HAS_0F_PREFIX, 0x82, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JNB,  ENC_HAS_0F_PREFIX, 0x83, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JZ,   ENC_HAS_0F_PREFIX, 0x84, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JNZ,  ENC_HAS_0F_PREFIX, 0x85, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JBE,  ENC_HAS_0F_PREFIX, 0x86, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JNBE, ENC_HAS_0F_PREFIX, 0x87, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JS,   ENC_HAS_0F_PREFIX, 0x88, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JNS,  ENC_HAS_0F_PREFIX, 0x89, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JP,   ENC_HAS_0F_PREFIX, 0x8a, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JNP,  ENC_HAS_0F_PREFIX, 0x8b, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JL,   ENC_HAS_0F_PREFIX, 0x8c, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JNL,  ENC_HAS_0F_PREFIX, 0x8d, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JLE,  ENC_HAS_0F_PREFIX, 0x8e, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_JNLE, ENC_HAS_0F_PREFIX, 0x8f, 0x0, { { OP_REL, 16 | 32 } } };

	// test
	*(e++) = (E) { MNEMONIC_TEST, ENC_NONE, 0x84, 0x0, { { OP_RM, 8 },            { OP_REG, 8 } } };
	*(e++) = (E) { MNEMONIC_TEST, ENC_NONE, 0x84, 0x0, { { OP_RM, 16 | 32 | 64 }, { OP_REG, 16 | 32 | 64 } } };

	// set[z,nz,be,nbe,s,ns,p,no,l,nl,nle]
	*(e++) = (E) { MNEMONIC_SETZ,   ENC_HAS_0F_PREFIX, 0x94, 0x0, { { OP_RM, 8 } } }; 
	*(e++) = (E) { MNEMONIC_SETNZ,  ENC_HAS_0F_PREFIX, 0x95, 0x0, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_SETBE,  ENC_HAS_0F_PREFIX, 0x96, 0x0, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_SETNBE, ENC_HAS_0F_PREFIX, 0x97, 0x0, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_SETS,   ENC_HAS_0F_PREFIX, 0x98, 0x0, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_SETNS,  ENC_HAS_0F_PREFIX, 0x99, 0x0, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_SETP,   ENC_HAS_0F_PREFIX, 0x9a, 0x0, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_SETNP,  ENC_HAS_0F_PREFIX, 0x9b, 0x0, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_SETL,   ENC_HAS_0F_PREFIX, 0x9c, 0x0, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_SETNL,  ENC_HAS_0F_PREFIX, 0x9d, 0x0, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_SETLE,  ENC_HAS_0F_PREFIX, 0x9e, 0x0, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_SETNLE, ENC_HAS_0F_PREFIX, 0x9f, 0x0, { { OP_RM, 8 } } };

	// mov
	*(e++) = (E) { MNEMONIC_MOV, ENC_NONE, 0x88, 0x0, { { OP_RM,  8 },            { OP_REG, 8 } } };
	*(e++) = (E) { MNEMONIC_MOV, ENC_NONE, 0x89, 0x0, { { OP_RM,  16 | 32 | 64 }, { OP_REG, 16 | 32 | 64 } } };
	*(e++) = (E) { MNEMONIC_MOV, ENC_NONE, 0x8a, 0x0, { { OP_REG, 8 },            { OP_RM,  8 } } };
	*(e++) = (E) { MNEMONIC_MOV, ENC_NONE, 0x8b, 0x0, { { OP_REG, 16 | 32 | 64 }, { OP_RM,  16 | 32 | 64 } } };

	*(e++) = (E) { MNEMONIC_MOV, ENC_ADD_REG_TO_OPCODE, 0xb0, 0x0, { { OP_REG, 8 },            { OP_IMM, 8 } } };
	*(e++) = (E) { MNEMONIC_MOV, ENC_ADD_REG_TO_OPCODE, 0xb8, 0x0, { { OP_REG, 16 | 32 | 64 }, { OP_IMM, 16 | 32 | 64 } } };

	// lea
	*(e++) = (E) { MNEMONIC_LEA, ENC_NONE, 0x8d, 0x0, { { OP_REG, 16 | 32 | 64 }, { OP_MEM, 8 | 16 | 32 | 64 } } };

	// int3
	*(e++) = (E) { MNEMONIC_INT3, ENC_NONE, 0xcc, 0x0 };

	// mul
	*(e++) = (E) { MNEMONIC_MUL, ENC_NONE, 0xf6, 0x4, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_MUL, ENC_NONE, 0xf7, 0x4, { { OP_RM, 16 | 32 | 64 } } };

	// imul
	*(e++) = (E) { MNEMONIC_IMUL, ENC_NONE, 0x69, 0x0, {
		{ OP_REG, 16 | 32 | 64 },
		{ OP_RM,  16 | 32 | 64 },
		{ OP_IMM, 16 | 32 }
	} };
	*(e++) = (E) { MNEMONIC_IMUL, ENC_NONE, 0x6b, 0x0, {
		{ OP_REG, 16 | 32 | 64 },
		{ OP_RM,  16 | 32 | 64 },
		{ OP_IMM, 8 }
	} };
	*(e++) = (E) { MNEMONIC_IMUL, ENC_NONE, 0xf6, 0x5, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_IMUL, ENC_NONE, 0xf7, 0x5, { { OP_RM, 16 | 32 | 64 } } };

	*(e++) = (E) { MNEMONIC_IMUL, ENC_HAS_0F_PREFIX, 0xaf, 0x0, { { OP_REG, 16 | 32 | 64 }, { OP_RM, 16 | 32 | 64 } } };

	// div
	*(e++) = (E) { MNEMONIC_DIV, ENC_NONE, 0xf6, 0x6, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_DIV, ENC_NONE, 0xf7, 0x6, { { OP_RM, 16 | 32 | 64 } } };

	// idiv
	*(e++) = (E) { MNEMONIC_IDIV, ENC_NONE, 0xf6, 0x7, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_IDIV, ENC_NONE, 0xf7, 0x7, { { OP_RM, 16 | 32 | 64 } } };

	// movzx
	*(e++) = (E) { MNEMONIC_MOVZX, ENC_HAS_0F_PREFIX, 0xb6, 0x0, { { OP_REG, 16 | 32 | 64 }, { OP_RM, 8  } } };
	*(e++) = (E) { MNEMONIC_MOVZX, ENC_HAS_0F_PREFIX, 0xb7, 0x0, { { OP_REG, 16 | 32 | 64 }, { OP_RM, 16 } } };

	// cwd
	*(e++) = (E) { MNEMONIC_CWD, ENC_MANDATORY_66, 0x99, 0x0 };
	// cdq
	*(e++) = (E) { MNEMONIC_CDQ, ENC_NONE, 0x99, 0x0 };
	// cqo
	*(e++) = (E) { MNEMONIC_CQO, ENC_MANDATORY_REX_64, 0x99, 0x0 };

	// movsx
	*(e++) = (E) { MNEMONIC_MOVSX, ENC_HAS_0F_PREFIX, 0xbe, 0x0, { { OP_REG, 16 | 32 | 64 }, { OP_RM, 8  } } };
	*(e++) = (E) { MNEMONIC_MOVSX, ENC_HAS_0F_PREFIX, 0xbf, 0x0, { { OP_REG, 16 | 32 | 64 }, { OP_RM, 16 } } };

	// movsxd
	*(e++) = (E) { MNEMONIC_MOVSXD, ENC_NONE, 0x63, 0x0, { { OP_REG, 32 | 64 }, { OP_RM, 32 } } };

	// shr
	*(e++) = (E) { MNEMONIC_SHR, ENC_NONE, 0xc0, 0x5, { { OP_RM, 8 },            { OP_IMM, 8 } } };
	*(e++) = (E) { MNEMONIC_SHR, ENC_NONE, 0xc1, 0x5, { { OP_RM, 16 | 32 | 64 }, { OP_IMM, 8 } } };
	*(e++) = (E) { MNEMONIC_SHR, ENC_NONE, 0xd2, 0x5, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_SHR, ENC_NONE, 0xd3, 0x5, { { OP_RM, 16 | 32 | 64 } } };

	// shl
	*(e++) = (E) { MNEMONIC_SHL, ENC_NONE, 0xc0, 0x4, { { OP_RM, 8 },            { OP_IMM, 8 } } };
	*(e++) = (E) { MNEMONIC_SHL, ENC_NONE, 0xc1, 0x4, { { OP_RM, 16 | 32 | 64 }, { OP_IMM, 8 } } };
	*(e++) = (E) { MNEMONIC_SHL, ENC_NONE, 0xd2, 0x4, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_SHL, ENC_NONE, 0xd3, 0x4, { { OP_RM, 16 | 32 | 64 } } };

	// ret
	*(e++) = (E) { MNEMONIC_RET, ENC_NONE, 0xc3, 0x0 };

	// jmp
	*(e++) = (E) { MNEMONIC_JMP, ENC_NONE, 0xe9, 0x0, { { OP_REL, 16 | 32 } } };

	// not
	*(e++) = (E) { MNEMONIC_NOT, ENC_NONE, 0xf6, 0x2, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_NOT, ENC_NONE, 0xf7, 0x2, { { OP_RM, 16 | 32 | 64 } } };

	// neg
	*(e++) = (E) { MNEMONIC_NEG, ENC_NONE, 0xf6, 0x3, { { OP_RM, 8 } } };
	*(e++) = (E) { MNEMONIC_NEG, ENC_NONE, 0xf7, 0x3, { { OP_RM, 16 | 32 | 64 } } };

	// call
	*(e++) = (E) { MNEMONIC_CALL, ENC_NONE, 0xe8, 0x0, { { OP_REL, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_CALL, ENC_NONE, 0xff, 0x2, { { OP_RM, 16 | 32 } } };
	*(e++) = (E) { MNEMONIC_CALL, ENC_NONE, 0xff, 0x2, { { OP_RM, 64 } } };

	assert(e == s_encodings + array_size(s_encodings));

	{
		MnemonicKind current_mnemonic = s_encodings[0].mnemonic;
		s_encoding_ranges[current_mnemonic] = (EncodingRange) {};
		for (size_t i = 1; i < array_size(s_encodings); i += 1) {
			s_encoding_ranges[current_mnemonic].end = i;

			if (s_encodings[i].mnemonic != current_mnemonic) {
				current_mnemonic = s_encodings[i].mnemonic;
				assert_msg(s_encoding_ranges[current_mnemonic].start == 0
						&& s_encoding_ranges[current_mnemonic].end == 0,
						"Different encodings for the same mnemonic must be grouped together and "
						"appear as subsequent array elements");

				s_encoding_ranges[current_mnemonic].start = i;
				s_encoding_ranges[current_mnemonic].end = i;
			}
		}

		s_encoding_ranges[current_mnemonic].end = array_size(s_encodings);
	}

	{
		for (size_t i = 0; i < array_size(s_encodings); i += 1) {
			Encoding encoding = s_encodings[i];

			size_t count = 0;
			size_t reg_op_count = 0;
			for (count = 0; count < MAX_OPERAND_COUNT; count += 1) {
				if (encoding.operands[count].kind == OP_NONE) {
					break;
				} else if (has_any_flag(encoding.operands[count].kind, OP_RM)) {
					reg_op_count += 1;
				}
			}

			s_encoding_extra[i].operand_count = (uint8_t)count;
			s_encoding_extra[i].reg_operand_count = (uint8_t)reg_op_count;

			if (has_flag(encoding.flags, ENC_ADD_REG_TO_OPCODE)) {
				assert_msg(reg_op_count >= 1,
						"Encoding requires adding the register index to the"
						" opcode byte, however it has no operands of register or memory type");

				reg_op_count -= 1;
			}

			if (reg_op_count > 0) {
				s_encoding_extra[i].has_mod_rm = true;
			}
		}
	}

	s_encoding_initialized = true;

	profile_scope_end();
}

typedef struct {
	ModRM mod;
	uint8_t reg;
	uint8_t rm;

	int32_t displacement;
} ModRMFields;

static ModRMFields _encode_mod_rm(Encoding encoding, Operand op0, Operand op1) {
	assert_msg(!has_flag(encoding.flags, ENC_ADD_REG_TO_OPCODE),
			"Instructions that encode a register in the opcode byte don't have a ModR/M byte");
	
	Operand operands[2] = { op0, op1 };

	ModRMFields fields = {};
	fields.mod = MOD_RM_RM;

	for (size_t i = 0; i < 2; i += 1) {
		OperandKind op_mask = encoding.operands[i].kind;
		if (op_mask == OP_NONE) {
			continue;
		}

		Operand op = operands[i];
		if (op.kind == OP_MEM && op.extra.mem.is_rip_relative) {
			assert(op.mem.base_reg == 5 || op.mem.base_reg == 13);
			fields.mod = MOD_RM_ADDRESS_RM;
			fields.displacement = op.mem.disp;
		} else if (op.kind == OP_MEM) {
			// NOTE: [bp] and [r13] with MOD_RM_ADDRESS_RM are used for addressing relative to
			//       instruction pointer. If we want to address bp/r13, we need to use a different
			//       addressing mode (with 8-bit zero displacement).
			if (op.mem.base_reg == 5 || op.mem.base_reg == 13) {
				fields.mod = MOD_RM_ADDRESS_RM_DISP_8;
				fields.displacement = 0;
			} else {
				fields.mod = MOD_RM_ADDRESS_RM;
			}

			if (op.mem.disp == 0) {
				// No displacement
			} else if (op.mem.disp <= INT8_MAX) {
				fields.mod = MOD_RM_ADDRESS_RM_DISP_8;
				fields.displacement = op.mem.disp;
			} else {
				fields.mod = MOD_RM_ADDRESS_RM_DISP_32;
				fields.displacement = op.mem.disp;
			}
		}

		uint8_t reg = 0;
		switch (op.kind) {
		case OP_REG:
			assert(op.reg < X64_GPR_COUNT);
			reg = op.reg;
			break;
		case OP_MEM:
			assert(op.mem.base_reg < X64_GPR_COUNT);
			reg = op.mem.base_reg;
			break;
		case OP_IMM:
			break;
		}

		if (op_mask == (OP_REG | OP_MEM)) {
			fields.rm = reg;
		} else if (op_mask == OP_REG) {
			fields.reg = reg;
		} else if (op_mask == OP_MEM) {
			fields.rm = reg;
		} else if (op_mask == OP_IMM) {
		} else {
			unreachable();
		}
	}

	return fields;
}

static size_t _select_encoding(MnemonicKind mnemonic,
		const Operand* operands,
		uint8_t operand_count) {
	profile_scope_start(__func__);

	if (!s_encoding_initialized) {
		encoding_init();
	}

	assert(mnemonic <= MNEMONIC_COUNT);

	size_t selected_index = SIZE_MAX;

	EncodingRange range = s_encoding_ranges[mnemonic];
	for (size_t i = range.start; i < range.end; i += 1) {
		if (operand_count != s_encoding_extra[i].operand_count) {
			continue;
		}

		Encoding encoding = s_encodings[i];

		bool is_supported = true;
		for (uint8_t j = 0; j < operand_count; j += 1) {
			if (has_flag(encoding.operands[j].kind, operands[j].kind)
				&& has_flag(encoding.operands[j].sizes, operands[j].bit_count)) {
				continue;
			} else {
				is_supported = false;
				break;
			}
		}

		if (!is_supported) {
			continue;
		}

		selected_index = i;
		break;
	}

	profile_scope_end();
	return selected_index;
}

void encode_n(CodeBuffer* code_buffer,
		MnemonicKind mnemonic,
		const Operand* operands,
		uint8_t operand_count) {
	profile_scope_start(__func__);

	assert(code_buffer != NULL);
	assert(operand_count <= MAX_OPERAND_COUNT);

	size_t encoding_index = _select_encoding(mnemonic, operands, operand_count);
	if (encoding_index == SIZE_MAX) {
		panic("No encoding found");
	}

	Encoding encoding = s_encodings[encoding_index];
	uint8_t rex_prefix_bits = 0;
	bool requires_empty_sib_byte = 0;

	ModRMFields fields = {};
	if (s_encoding_extra[encoding_index].has_mod_rm) {
		if (operand_count == 1) {
			fields = _encode_mod_rm(encoding, operands[0], operand_none());
		} else if (operand_count == 2) {
			fields = _encode_mod_rm(encoding, operands[0], operands[1]);

			if (operands[0].kind == OP_MEM &&
					(operands[0].mem.base_reg == 4 || operands[0].mem.base_reg == 12)) {
				requires_empty_sib_byte = true;
			}

			if (operands[1].kind == OP_MEM &&
					(operands[1].mem.base_reg == 4 || operands[1].mem.base_reg == 12)) {
				requires_empty_sib_byte = true;
			}
		} else {
			unreachable();
		}

		if (s_encoding_extra[encoding_index].reg_operand_count == 1) {
			fields.reg = encoding.mod_rm_ext;
		}

		rex_prefix_bits |= ((fields.reg >> 3) << 2);
		rex_prefix_bits |= ((fields.rm >> 3) << 0);
	} else if (operand_count >= 1) {
		if (operands[0].kind == OP_REG) {
			if (has_flag(encoding.flags, ENC_ADD_REG_TO_OPCODE)) {
				// For instructions that require adding register number to the opcode byte, store
				// the register extension in the B field of the rex prefix instead of R
				rex_prefix_bits |= (operands[0].reg >> 3); // rm field
			} else {
				rex_prefix_bits |= (operands[0].reg >> 3) << 2; // reg field
			}
		}
	}

	for (size_t i = 0; i < operand_count; i += 1) {
		Operand op = operands[i];
		if (op.bit_count == 64 && (op.kind == OP_REG)) {
			rex_prefix_bits |= 0b1000;
		}
	}

	// now fill the bytes
	uint8_t encoding_buffer[MAX_ENCODING_SIZE];

	uint8_t* buffer = encoding_buffer;
	uint8_t* write_ptr = buffer;

	// operand size override
	bool is_16_bit = operand_count > 0 && operands[0].bit_count == 16;
	if (is_16_bit || has_flag(encoding.flags, ENC_MANDATORY_66)) {
		*write_ptr = 0x66;
		write_ptr += 1;
	}

	// rex prefix
	if (rex_prefix_bits) {
		*write_ptr = 0b01000000 | rex_prefix_bits;
		write_ptr += 1;
	} else if (has_flag(encoding.flags, ENC_MANDATORY_REX_64)) {
		*write_ptr = 0b01000000 | 0b1000; // 64-bit operand
		write_ptr += 1;
	}

	// 0f prefix
	if (has_flag(encoding.flags, ENC_HAS_0F_PREFIX)) {
		*write_ptr = 0x0f;
		write_ptr += 1;
	}

	// opcode byte
	if (has_flag(encoding.flags, ENC_ADD_REG_TO_OPCODE)) {
		assert(operands[0].kind == OP_REG);
		*write_ptr = encoding.opcode + (operands[0].reg & 0b111);
	} else {
		*write_ptr = encoding.opcode;
	}

	write_ptr += 1;

	// morm byte
	if (s_encoding_extra[encoding_index].has_mod_rm) {
		*write_ptr = ((fields.reg & 0b111) << 3) | (fields.rm & 0b111) | fields.mod;
		write_ptr += 1;
	}

	// sib byte
	if (requires_empty_sib_byte) {
		*write_ptr = 0x24;
		write_ptr += 1;
	}

	// disaplacement
	if (fields.mod == MOD_RM_ADDRESS_RM_DISP_8) {
		*write_ptr = (int8_t)fields.displacement;
		write_ptr += 1;
	} else if (fields.mod == MOD_RM_ADDRESS_RM_DISP_32) {
		memcpy(write_ptr, &fields.displacement, 4);
		write_ptr += 4;
	} else if ((fields.rm == 5 || fields.rm == 13) && fields.mod == MOD_RM_ADDRESS_RM) {
		// See addressing relative to `RIP` register
		memcpy(write_ptr, &fields.displacement, 4);
		write_ptr += 4;
	}

	// write imm
	for (size_t i = 0; i < operand_count; i += 1) {
		if (operands[i].kind == OP_IMM) {
			memcpy(write_ptr, &operands[i].imm, operands[i].bit_count / 8);
			write_ptr += operands[i].bit_count / 8;
		} else if (operands[i].kind == OP_REL) {
			memcpy(write_ptr, &operands[i].rel, sizeof(operands[i].rel));
			write_ptr += sizeof(operands[i].rel);
		}
	}

	size_t encoding_length = write_ptr - buffer;
	assert(encoding_length <= MAX_ENCODING_SIZE);

	uint8_t* output_buffer = code_buffer_append(code_buffer, encoding_length);
	memcpy(output_buffer, encoding_buffer, encoding_length);

	code_buffer->instruction_count += 1;

	profile_scope_end();
}
