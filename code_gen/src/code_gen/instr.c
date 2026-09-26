#include "instr.h"

bool instr_bin_op_is_commutative(InstrBinOp op) {
	switch (op) {
	case INSTR_BIN_ADD:
	case INSTR_BIN_IMUL:
	case INSTR_BIN_UMUL:
		return true;
	case INSTR_BIN_SUB:
	case INSTR_BIN_IDIV:
	case INSTR_BIN_UDIV:
	case INSTR_BIN_IMOD:
	case INSTR_BIN_UMOD:
		return false;
	case INSTR_BIN_AND:
	case INSTR_BIN_OR:
	case INSTR_BIN_XOR:
		return true;
	case INSTR_BIN_SHIFT_LEFT:
	case INSTR_BIN_SHIFT_RIGHT:
		return false;
	}

	unreachable();
	return false;
}

InstrCompareKind instr_compare_kind_flip(InstrCompareKind kind) {
	switch (kind) {
	case INSTR_CMP_EQUAL:
		return INSTR_CMP_NOT_EQUAL;
	case INSTR_CMP_NOT_EQUAL:
		return INSTR_CMP_EQUAL;
	case INSTR_CMP_LESS:
		return INSTR_CMP_GREATER_OR_EQUAL;
	case INSTR_CMP_LESS_OR_EQUAL:
		return INSTR_CMP_GREATER;
	case INSTR_CMP_GREATER:
		return INSTR_CMP_LESS_OR_EQUAL;
	case INSTR_CMP_GREATER_OR_EQUAL:
		return INSTR_CMP_LESS;
	}

	unreachable();
	return 0;
}

InstrFeatureFlag INSTR_FEATURES[INSTR_COUNT] = {
	[INSTR_NO_OP] = 0,

	[INSTR_UNINITIALIZED_8] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_UNINITIALIZED_16] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_UNINITIALIZED_32] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_UNINITIALIZED_64] = INSTR_FEATURE_REG_STORAGE,

	[INSTR_CONST_8] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_CONST_16] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_CONST_32] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_CONST_64] = INSTR_FEATURE_REG_STORAGE,

	[INSTR_CONST_STRING] = INSTR_FEATURE_REG_STORAGE,

	[INSTR_BIN_OP_8] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_BIN_OP_16] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_BIN_OP_32] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_BIN_OP_64] = INSTR_FEATURE_REG_STORAGE,

	[INSTR_NEGATE_8] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_NEGATE_16] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_NEGATE_32] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_NEGATE_64] = INSTR_FEATURE_REG_STORAGE,

	[INSTR_PTR_LOAD_8] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_PTR_LOAD_16] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_PTR_LOAD_32] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_PTR_LOAD_64] = INSTR_FEATURE_REG_STORAGE,

	[INSTR_PTR_STORE_8] = 0,
	[INSTR_PTR_STORE_16] = 0,
	[INSTR_PTR_STORE_32] = 0,
	[INSTR_PTR_STORE_64] = 0,

	[INSTR_BITWISE_NOT_8] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_BITWISE_NOT_16] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_BITWISE_NOT_32] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_BITWISE_NOT_64] = INSTR_FEATURE_REG_STORAGE,

	[INSTR_NOT] = INSTR_FEATURE_BOOL,
	[INSTR_LOGICAL_AND] = INSTR_FEATURE_BOOL,
	[INSTR_LOGICAL_OR] = INSTR_FEATURE_BOOL,

	[INSTR_COMPARE_8] = INSTR_FEATURE_BOOL,
	[INSTR_COMPARE_16] = INSTR_FEATURE_BOOL,
	[INSTR_COMPARE_32] = INSTR_FEATURE_BOOL,
	[INSTR_COMPARE_64] = INSTR_FEATURE_BOOL,

	[INSTR_BOOL_TO_INT] = INSTR_FEATURE_REG_STORAGE,

	[INSTR_UNSIGNED_EXTEND_TO_16] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_UNSIGNED_EXTEND_TO_32] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_UNSIGNED_EXTEND_TO_64] = INSTR_FEATURE_REG_STORAGE,

	[INSTR_SIGNED_EXTEND_TO_16] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_SIGNED_EXTEND_TO_32] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_SIGNED_EXTEND_TO_64] = INSTR_FEATURE_REG_STORAGE,

	[INSTR_REDUCE_TO_8] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_REDUCE_TO_16] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_REDUCE_TO_32] = INSTR_FEATURE_REG_STORAGE,

	[INSTR_LOAD_ARG_8] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_LOAD_ARG_16] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_LOAD_ARG_32] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_LOAD_ARG_64] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_LOAD_ARG_STACK] = INSTR_FEATURE_STACK_STORAGE,

	[INSTR_STACK_ALLOC] = INSTR_FEATURE_STACK_STORAGE,
	[INSTR_STACK_ADDR] = INSTR_FEATURE_REG_STORAGE,

	[INSTR_BRANCH] = INSTR_FEATURE_CONTROL,
	[INSTR_JUMP] = INSTR_FEATURE_CONTROL,

	[INSTR_RETURN_VALUE] = INSTR_FEATURE_CONTROL,
	[INSTR_RET] = INSTR_FEATURE_CONTROL,

	[INSTR_LOAD_FUNCTION_ADDR] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_LOAD_EXTERNAL_FUNCTION_ADDR] = INSTR_FEATURE_REG_STORAGE,

	[INSTR_CALL_DIRECT] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_CALL_INDIRECT] = INSTR_FEATURE_REG_STORAGE,

	[INSTR_PHI] = INSTR_FEATURE_REG_STORAGE,
	[INSTR_SELECT] = INSTR_FEATURE_NONE,

	[INSTR_IO_STATE] = 0,
	[INSTR_REGION] = 0,
};

//
// InstrBuffer
//

void instr_buffer_init(InstrBuffer* buffer, Arena* allocator) {
	buffer->instr = arena_alloc_array(allocator, Instr, 0);
	buffer->inputs_buffer = NULL;
	buffer->count = 0;
	buffer->inputs_buffer_size = 0;
	buffer->inputs_buffer_capacity = 0;
	buffer->region_count = 0;
}

void instr_buffer_release(InstrBuffer* buffer) {
	if (buffer->inputs_buffer) {
		heap_release(buffer->inputs_buffer);
	}

	*buffer = (InstrBuffer) {};
}

InstrInputs instr_allocate_inputs_array(InstrBuffer* buffer, uint16_t count) {
	profile_scope_start(__func__);

	assert(buffer->inputs_buffer_capacity >= buffer->inputs_buffer_size);

	if (count == 0) {
		profile_scope_end();
		return (InstrInputs) { .start = UINT16_MAX, .count = 0 };
	}

	uint16_t free_size = buffer->inputs_buffer_capacity - buffer->inputs_buffer_size;
	if (count > free_size) {
		uint16_t new_capacity = max(32, buffer->inputs_buffer_capacity * 2);
		InstrIndex* new_buffer = heap_alloc_array(InstrIndex, new_capacity);

		if (buffer->inputs_buffer) {
			array_copy(new_buffer, buffer->inputs_buffer, buffer->inputs_buffer_size);
			heap_release(buffer->inputs_buffer);
		}

		asan_poison_memory_region(buffer->inputs_buffer, new_capacity - buffer->inputs_buffer_size);

		buffer->inputs_buffer = new_buffer;
		buffer->inputs_buffer_capacity = new_capacity;
	}

	InstrInputs inputs = {};
	inputs.start = buffer->inputs_buffer_size;
	inputs.count = count;

	buffer->inputs_buffer_size += count;
	assert(buffer->inputs_buffer_size <= buffer->inputs_buffer_capacity);

	profile_scope_end();
	return inputs;
}

//
// Instr
//

InstrIndex instr_new_int_const(InstrBuffer* buffer,
		Arena* allocator,
		uint64_t value,
		size_t int_size) {
	InstrIndex instr_index = instr_buffer_append(buffer, allocator);
	Instr* instr = instr_buffer_at(buffer, instr_index);

	switch (int_size) {
	case 1:
		assert(value <= 0xff);
		instr->kind = INSTR_CONST_8;
		instr->const_8.u = (uint8_t)value;
		break;
	case 2:
		assert(value <= 0xffff);
		instr->kind = INSTR_CONST_16;
		instr->const_16.u = (uint16_t)value;
		break;
	case 4:
		assert(value <= 0xffffffff);
		instr->kind = INSTR_CONST_32;
		instr->const_32.u = (uint32_t)value;
		break;
	case 8:
		assert(value <= 0xffffffffffffffff);
		instr->kind = INSTR_CONST_64;
		instr->const_64.u = value;
		break;
	default:
		unreachable();
	}

	return instr_index;
}

InstrIndex instr_new_jump(InstrBuffer* buffer,
		Arena* allocator,
		InstrIndex target,
		InstrIndex* io_state) {

	const Instr* io_state_instr = instr_buffer_at(buffer, *io_state);
	assert(io_state_instr->kind == INSTR_IO_STATE);

	InstrIndex i = instr_buffer_append(buffer, allocator);
	Instr* instr = instr_buffer_at(buffer, i);
	instr->kind = INSTR_JUMP;
	instr->jump.target_region = target;
	instr->jump.io_state = *io_state;

	*io_state = instr_new_io_state(buffer, allocator, INVALID_INSTR_INDEX);
	return i;
}

InstrIndex instr_new_return_value(InstrBuffer* buffer,
		Arena* allocator,
		InstrIndex value,
		InstrIndex* io_state) {

	const Instr* io_state_instr = instr_buffer_at(buffer, *io_state);
	assert(io_state_instr->kind == INSTR_IO_STATE);

	InstrIndex i = instr_buffer_append(buffer, allocator);
	Instr* instr = instr_buffer_at(buffer, i);
	instr->kind = INSTR_RETURN_VALUE;
	instr->return_value.value = value;
	instr->return_value.io_state = *io_state;

	*io_state = INVALID_INSTR_INDEX;
	return i;
}

InstrIndex instr_new_return(InstrBuffer* buffer, Arena* allocator, InstrIndex* io_state) {
	const Instr* io_state_instr = instr_buffer_at(buffer, *io_state);
	assert(io_state_instr->kind == INSTR_IO_STATE);

	InstrIndex i = instr_buffer_append(buffer, allocator);
	Instr* instr = instr_buffer_at(buffer, i);
	instr->kind = INSTR_RET;
	instr->ret.io_state = *io_state;

	*io_state = INVALID_INSTR_INDEX;
	return i;
}

InstrIndex instr_new_empty_phi(InstrBuffer* buffer, Arena* allocator) {
	InstrIndex i = instr_buffer_append(buffer, allocator);
	Instr* instr = instr_buffer_at(buffer, i);
	instr->kind = INSTR_PHI;
	instr->phi.variants.count = 0;
	return i;
}

InstrIndex instr_new_logical_shift_left_by(InstrBuffer* buffer,
		Arena* allocator,
		InstrIndex operand,
		uint8_t operand_size,
		uint8_t shift_count) {

	if (shift_count == 0) {
		return operand;
	}

	InstrIndex shift_count_const = instr_new_int_const(buffer,
			allocator,
			shift_count,
			1);

	InstrIndex shift_index = instr_buffer_append(buffer, allocator);
	Instr* shift_instr = instr_buffer_at(buffer, shift_index);
	shift_instr->bin_op.kind = INSTR_BIN_SHIFT_LEFT;
	shift_instr->bin_op.left = operand;
	shift_instr->bin_op.right = shift_count_const;

	switch (operand_size) {
	case 1:
		shift_instr->kind = INSTR_BIN_OP_8;
		break;
	case 2:
		shift_instr->kind = INSTR_BIN_OP_16;
		break;
	case 4:
		shift_instr->kind = INSTR_BIN_OP_32;
		break;
	case 8:
		shift_instr->kind = INSTR_BIN_OP_64;
		break;
	default:
		unreachable();
	}

	return shift_index;
}

InstrIndex instr_new_cast(InstrBuffer* buffer,
		Arena* allocator,
		InstrIndex value,
		uint8_t value_size,
		uint8_t target_size,
		bool is_signed) {
	if (value_size == target_size) {
		return value;
	}

	if (value_size < target_size) {
		InstrKind kind = INSTR_NO_OP;
		switch (target_size) {
		case 1:
			panic("Cannot extend to an 8-bit interger");
		case 2:
			kind = is_signed ? INSTR_SIGNED_EXTEND_TO_16 : INSTR_UNSIGNED_EXTEND_TO_16;
			break;
		case 4:
			kind = is_signed ? INSTR_SIGNED_EXTEND_TO_32 : INSTR_UNSIGNED_EXTEND_TO_32;
			break;
		case 8:
			kind = is_signed ? INSTR_SIGNED_EXTEND_TO_64 : INSTR_UNSIGNED_EXTEND_TO_64;
			break;
		default:
			panic("Unsupported target size");
		}

		assert(kind != INSTR_NO_OP);
		return instr_buffer_push(buffer, allocator, (Instr) {
			.kind = kind,
			.extend = {
				.value = value,
				.value_bit_count = value_size * 8,
			}
		});
	} else {
		InstrKind kind = INSTR_NO_OP;
		switch (target_size) {
		case 1:
			kind = INSTR_REDUCE_TO_8;
			break;
		case 2:
			kind = INSTR_REDUCE_TO_16;
			break;
		case 4:
			kind = INSTR_REDUCE_TO_32;
			break;
		case 8:
			panic("Cannot reduce down to a 64-bit interger");
		default:
			panic("Unsupported target size");
		}

		assert(kind != INSTR_NO_OP);
		return instr_buffer_push(buffer, allocator, (Instr) {
			.kind = kind,
			.extend = {
				.value = value,
				.value_bit_count = value_size * 8,
			}
		});
	}

	unreachable();
	return INVALID_INSTR_INDEX;
}

uint16_t instr_region_id(const InstrBuffer* buffer, InstrIndex region_index) {
	const Instr* instr = &buffer->instr[region_index.value];
	assert(instr->kind == INSTR_REGION);

	uint16_t id = instr->region.id;
	assert(id < buffer->region_count);
	return id;
}

bool instr_region_finished(const InstrBuffer* buffer, InstrIndex region_index) {
	const Instr* instr = instr_buffer_at(buffer, region_index);
	InstrIndex last_instr_in_region = instr->region.last_instr;

	if (last_instr_in_region.value == INVALID_INSTR_INDEX.value) {
		return false;
	}

	InstrKind last_instr_kind = buffer->instr[last_instr_in_region.value].kind;
	assert(has_flag(INSTR_FEATURES[last_instr_kind], INSTR_FEATURE_CONTROL));
	return true;
}

void instr_push_input_dependencies(const InstrBuffer* buffer,
		InstrInputs inputs,
		InstrQueue* out_dependencies) {

	for (uint16_t i = 0; i < inputs.count; i += 1) {
		instr_queue_push_back(out_dependencies, buffer->inputs_buffer[inputs.start + i]);
	}
}

//
// Live Ranges
//

inline bool _live_range_is_valid(const InstrLiveRange range) {
	return range.value != UINT32_MAX && range.start <= range.end;
}

inline bool _live_range_is_empty(const InstrLiveRange range) {
	return range.value == UINT32_MAX;
}

inline InstrLiveRange _live_range_merge(InstrLiveRange a, InstrLiveRange b) {
	InstrLiveRange out = {};
	out.start = min(a.start, b.start);
	out.end = max(a.end, b.end);
	return out;
}

// Returns a new range that includes the given instruction index
inline InstrLiveRange _live_range_extended(const InstrLiveRange range, uint64_t global_position) {
	InstrLiveRange new_range;
	new_range.start = min(range.start, global_position);
	new_range.end = max(range.end, global_position);
	return new_range;
}

InstrLiveRange* instr_compute_live_ranges(const InstrBuffer buffer,
		InstrIndex root_instr,
		InstrIndexArray scheduled_regions,
		InstrIndexArray* scheduled_instr,
		const CFGDominatorTree* dom_tree,
		Arena* allocator,
		Arena* temp_allocator) {
	profile_scope_start(__func__);

	ArenaRegion temp = arena_begin_temp(temp_allocator);

	uint16_t* instr_global_position = arena_alloc_array(temp_allocator, uint16_t, buffer.count);

	uint16_t next_global_position = 0;
	for (uint16_t region_index = 0; region_index < scheduled_regions.count; region_index += 1) {
		uint16_t region_id = instr_region_id(&buffer, scheduled_regions.instr[region_index]);
		InstrIndexArray instr = scheduled_instr[region_id];
		for (size_t i = 0; i < instr.count; i += 1) {
			instr_global_position[instr.instr[i].value] = next_global_position;
			next_global_position += 1;

			InstrKind kind = buffer.instr[instr.instr[i].value].kind;
			if (kind == INSTR_LOAD_ARG_8
					|| kind == INSTR_LOAD_ARG_16
					|| kind == INSTR_LOAD_ARG_32
					|| kind == INSTR_LOAD_ARG_64) {
				instr_global_position[instr.instr[i].value] = 0;
			}
		}
	}

	InstrLiveRange* live_ranges = arena_alloc_array(allocator, InstrLiveRange, buffer.count);
	memset(live_ranges, 0xff, sizeof(*live_ranges) * buffer.count);

	InstrQueue stack;
	instr_queue_alloc(&stack, temp_allocator, buffer.count);

	BitArray visited_instr = bit_array_alloc(temp_allocator, buffer.count);
	bit_array_clear(&visited_instr);

	instr_queue_push_back(&stack, root_instr);

	while (stack.count) {
		InstrIndex instr_index = instr_queue_pop_back(&stack);
		if (instr_index.value == UINT16_MAX) {
			continue;
		}

		if (bit_array_get(&visited_instr, instr_index.value)) {
			// This instruction has already been visited
			continue;
		}

		bit_array_set(&visited_instr, instr_index.value, true);
		if (_live_range_is_valid(live_ranges[instr_index.value])) {
			live_ranges[instr_index.value] = _live_range_extended(
					live_ranges[instr_index.value],
					instr_global_position[instr_index.value]);
		} else if (_live_range_is_empty(live_ranges[instr_index.value])) {
			live_ranges[instr_index.value].start = instr_global_position[instr_index.value];
			live_ranges[instr_index.value].end = instr_global_position[instr_index.value];
		} else {
			panic("InstrLiveRange has invalid state");
		}

		size_t first_dep_index = stack.count;
		instr_enumerate_uses(&buffer, instr_index, &stack);

		// NOTE: The loop after this check is used to extend the usage range of this instruction
		//       dependencies. In this way data dependencies get defined for the later register
		//       allocation step. However some instructions are only used to specify an order
		//       dependency, one of them is `INSTR_IO_STATE`.
		InstrKind this_instr_kind = buffer.instr[instr_index.value].kind;
		if (this_instr_kind == INSTR_IO_STATE) {
			// Don't define any data dependencies
			continue;
		}

		if (this_instr_kind == INSTR_SELECT) {
			const Instr* select = &buffer.instr[instr_index.value];
			const Instr* region = &buffer.instr[select->select.region.value];

			// Keep the variant value alive until the end of the region. At the end of this region,
			// control flow trafers to a different region, where a `phi` instruction pick ups this
			// variant value.
			InstrLiveRange* live_range = &live_ranges[select->select.value.value];
			uint16_t exnteded_until = instr_global_position[region->region.last_instr.value];
			if (_live_range_is_valid(*live_range)) {
				*live_range = _live_range_extended(
						*live_range,
						exnteded_until);
			} else if (_live_range_is_empty(*live_range)) {
				live_range->start = exnteded_until;
				live_range->end = exnteded_until;
			} else {
				panic("InstrLiveRange has invalid state");
			}

			continue;
		}

		for (size_t i = first_dep_index; i < stack.count; i += 1) {
			InstrIndex dep_index = stack.buffer[i];
			if (dep_index.value >= buffer.count) {
				continue;
			}

			InstrLiveRange live_range = live_ranges[dep_index.value];
			if (_live_range_is_valid(live_range)) {
				live_range = _live_range_extended(
						live_range,
						instr_global_position[instr_index.value]);
			} else if (_live_range_is_empty(live_range)) {
				live_range.start = instr_global_position[instr_index.value];
				live_range.end = instr_global_position[instr_index.value];
			} else {
				panic("InstrLiveRange has invalid state");
			}

			live_ranges[dep_index.value] = live_range;
		}
	}

	// Here we need to extend live ranges of phis so that they meet the next requirement.
	//
	// Let's say we have a region `R0`. And some phi `PHI0` which selects a value `V0` from region
	// `R0`. `PHI0` is scheduled to execute in some other region, which is not `R0`.
	//
	// Whenever, the control flow reaches the end of `R0`, it will get transfered to some other
	// region `R1`. And if the control flow from `R1` will eventually reach the region where `PHI0`
	// is placed, we need to extend the live range of the `PHI0` to include the start of `R1`, so
	// that there is no gap between `V0` and `PHI0` live ranges.
	//
	//                     V0 live range  PHI0 live range (initial)  PHI0 live range (exteded)
	//  ...     ...              |              
	//   |       R0 <- V0        *
	//    \     /                
	//     \   /                                                        
	//       R1                                                         *
	//       |                                                          |
	//      ...                                                         |
	//       |                                                          |
	//       R2 <- PHI0                        *                        |
	//       |                                 |                        |
	//      ...                               ...                      ...
	//
	// More examples:
	// Here the control flow from `R0` splits into two paths, both of which eventually lead to
	// `PHI0`. In this case the extended live range of the `PHI0` convers the starts of both `R2`
	// and `R1`.
	// 
	//                   V0  PHI0 (initial)  PHI0 (exteded)
	//      ...          |              
	//       |           |
	//       R0 <- V0    *
	//      /  \         
	//     /    \
	//    R2     R1
	//    |      |                             *
	//   ...    ...                            |
	//    |      |                             |
	//     \    /                              |
	//      \  /                               |
	//       R3                                |
	//       |                                 |
	//      ...                                |
	//       |                                 |
	//       R4 <- PHI0           *            |
	//       |                    |            |
	//      ...                  ...
	for (uint16_t region_index = 0; region_index < scheduled_regions.count; region_index += 1) {
		uint16_t region_id = instr_region_id(&buffer, scheduled_regions.instr[region_index]);
		InstrIndexArray instr = scheduled_instr[region_id];

		for (size_t i = 0; i < instr.count; i += 1) {
			InstrIndex instr_index = instr.instr[i];
			assert(_live_range_is_valid(live_ranges[instr_index.value]));

			const Instr* instr = &buffer.instr[instr_index.value];
			if (instr->kind != INSTR_PHI) {
				continue;
			}

			uint16_t phi_region_id = region_id;

			InstrInputs variants = instr->phi.variants;
			for (uint16_t j = variants.start; j < variants.start + variants.count; j += 1) {
				InstrIndex select_index = buffer.inputs_buffer[j];
				const Instr* select = &buffer.instr[select_index.value];
				const Instr* region = &buffer.instr[select->select.region.value];

				InstrIndex paths[2];
				size_t path_count = 0;

				const Instr* last_instr = &buffer.instr[region->region.last_instr.value];
				switch (last_instr->kind) {
				case INSTR_JUMP:
					paths[0] = last_instr->jump.target_region;
					path_count = 1;
					break;
				case INSTR_BRANCH:
					paths[0] = last_instr->branch.true_region;
					paths[1] = last_instr->branch.false_region;
					path_count = 2;
					break;
				case INSTR_RET:
				case INSTR_RETURN_VALUE:
					break;
				default:
					unreachable();
				}

				bool all_paths_lead_to_phi = dom_tree_is_region_dominated_by(dom_tree,
						region->region.id,
						phi_region_id);

				for (size_t path_index = 0; path_index < path_count; path_index++) {
					uint16_t path_region_id = instr_region_id(&buffer, paths[path_index]);

					bool path_leads_to_phi = dom_tree_is_region_dominated_by(dom_tree,
							path_region_id,
							phi_region_id);

					if (!(path_leads_to_phi || all_paths_lead_to_phi)) {
						// The path doesn't lead to the `phi`
						continue;
					}

					InstrIndex first_instr_index = scheduled_instr[path_region_id].instr[0];
					uint16_t path_region_start = instr_global_position[first_instr_index.value];

					live_ranges[instr_index.value] = _live_range_extended(
							live_ranges[instr_index.value],
							path_region_start);
				}
			}
		}
	}

	arena_end_temp(temp);
	profile_scope_end();
	return live_ranges;
}

String instr_format_input_instrs(const InstrIndex* input_instr_buffer,
		InstrInputs inputs,
		Arena* temp_allocator) {
	StringBuilder builder = { .arena = temp_allocator };

	str_builder_append_char(&builder, '[');
	for (uint16_t i = 0; i < inputs.count; i += 1) {
		InstrIndex input = input_instr_buffer[inputs.start + i];
		str_builder_append(&builder, STR_LIT("\033[33;1m%"));
		str_builder_append_int(&builder, input.value);
		str_builder_append(&builder, STR_LIT("\033[0m"));

		if (i != inputs.count - 1) {
			str_builder_append(&builder, STR_LIT(", "));
		}
	}
	str_builder_append_char(&builder, ']');

	return builder.string;
}

void instr_print_all(InstrBuffer instr_buffer, Arena* temp_allocator) {
	for (size_t i = 0; i < instr_buffer.count; i += 1) {
		ArenaRegion temp = arena_begin_temp(temp_allocator);

		printf("%zu", i);
		printf("\033[12G");
		instr_print(&instr_buffer.instr[i], instr_buffer.inputs_buffer, temp_allocator);

		arena_end_temp(temp);
	}
}

//
// Dominator Tree
//

typedef InstrIndexArray(*CFGRegionNeighborsProvider)(const InstrBuffer* instr_buffer,
		InstrIndex region_index,
		void* user_data);

static CFGDominatorTree _dom_tree_build_with_neighbor_provider(const InstrBuffer* instr_buffer,
		InstrIndexArray initial_regions,
		CFGRegionNeighborsProvider provider,
		void* provider_user_data,
		Arena* allocator,
		Arena* temp_allocator) {
	profile_scope_start(__func__);
	ArenaRegion temp = arena_begin_temp(temp_allocator);

	BitArray visited_regions = bit_array_alloc(temp_allocator, instr_buffer->region_count);
	bit_array_clear(&visited_regions);

	InstrQueue stack;
	instr_queue_alloc(&stack, temp_allocator, instr_buffer->region_count);

	// Allocate the tree
	CFGDominatorTree tree;
	tree.region_count = instr_buffer->region_count;
	tree.dominates = arena_alloc_array(allocator, BitArray, tree.region_count);
	tree.parent = arena_alloc_array(allocator, uint16_t, tree.region_count);

	for (uint16_t i = 0; i < instr_buffer->region_count; i += 1) {
		tree.dominates[i] = bit_array_alloc(allocator, tree.region_count);
		bit_array_clear(&tree.dominates[i]);
		bit_array_set(&tree.dominates[i], i, true);
	}

	// Push the initial regions on the stack
	for (size_t i = 0; i < initial_regions.count; i += 1) {
		InstrIndex initial_region_index = initial_regions.instr[i];
		instr_queue_push_back(&stack, initial_region_index);

		const Instr* initial = instr_buffer_at(instr_buffer, initial_region_index);
		bit_array_set(&visited_regions, initial->region.id, true);

		tree.parent[initial->region.id] = UINT16_MAX;
	}
	
	// Build the tree
	while (stack.count) {
		InstrIndex region_instr_index = instr_queue_pop_back(&stack);
		const Instr* instr = instr_buffer_at(instr_buffer, region_instr_index);
		assert(instr->kind == INSTR_REGION);

		InstrIndexArray neighbors = provider(instr_buffer, region_instr_index, provider_user_data);
		
		for (size_t i = 0; i < neighbors.count; i += 1) {
			InstrIndex neighbor_index = neighbors.instr[i];
			const Instr* neighbor = instr_buffer_at(instr_buffer, neighbor_index);
			assert(neighbor->kind == INSTR_REGION);

			bool changed = false;
			if (bit_array_get(&visited_regions, neighbor->region.id)) {
				// Reset the bit corresponding to the current region, so it doesn't interfere with
				// the `and` operation.
				bit_array_set(&tree.dominates[neighbor->region.id], neighbor->region.id, false);

				changed |= bit_array_and(&tree.dominates[instr->region.id],
						&tree.dominates[neighbor->region.id],
						&tree.dominates[neighbor->region.id]);

				bit_array_set(&tree.dominates[neighbor->region.id], neighbor->region.id, true);
			} else {
				changed |= bit_array_or(&tree.dominates[instr->region.id],
						&tree.dominates[neighbor->region.id],
						&tree.dominates[neighbor->region.id]);

				bit_array_set(&visited_regions, neighbor->region.id, true);
			}

			// If the set of regions dominated by the current one, was updated, we need to continue
			// the traversal and propagate the updates to the neighbors.
			if (changed) {
				instr_queue_push_back(&stack, neighbor_index);
			}
		}
	}

	// Now determine immediate dominators.
	for (uint16_t i = 0; i < instr_buffer->region_count; i += 1) {
		BitArray* dominance = &tree.dominates[i];
		assert(bit_array_get(dominance, i));
		bit_array_set(dominance, i, false);

		bool found = false;
		for (uint16_t j = 0; j < instr_buffer->region_count; j += 1) {
			if (i == j) {
				continue;
			}

			if (bit_array_equal(dominance, &tree.dominates[j])) {
				tree.parent[i] = j;
				found = true;
				break;
			}
		}

		bit_array_set(dominance, i, true);

		if (!found) {
			// This region is unreachable
			tree.parent[i] = UINT16_MAX;
		}
	}

	// The initial region dones't have a parent
	for (size_t i = 0; i < initial_regions.count; i += 1) {
		InstrIndex initial_region_index = initial_regions.instr[i];
		assert(tree.parent[instr_region_id(instr_buffer, initial_region_index)] == UINT16_MAX);
	}

	arena_end_temp(temp);

	profile_scope_end();
	return tree;
}

static InstrIndexArray _region_successors_provider(const InstrBuffer* instr_buffer,
		InstrIndex region_index,
		void* data) {

	InstrIndex* successors = (InstrIndex*)data;
	size_t successor_count = 0;

	const Instr* region_instr = instr_buffer_at(instr_buffer, region_index);
	const Instr* last_instr = instr_buffer_at(instr_buffer, region_instr->region.last_instr);
	switch (last_instr->kind) {
	case INSTR_JUMP:
		successors[0] = last_instr->jump.target_region;
		successor_count = 1;
		break;
	case INSTR_BRANCH:
		successors[0] = last_instr->branch.true_region;
		successors[1] = last_instr->branch.false_region;
		successor_count = 2;
		break;
	case INSTR_RET:
	case INSTR_RETURN_VALUE:
		break;
	default:
		unreachable();
	}

	return (InstrIndexArray) {
		.instr = successors,
		.count = successor_count,
	};
}

CFGDominatorTree dom_tree_build(const InstrBuffer* instr_buffer,
		InstrIndex initial_region,
		Arena* allocator,
		Arena* temp_allocator) {

	InstrIndex successor_buffer[2];
	InstrIndexArray initial_regions = { .instr = &initial_region, .count = 1 };

	return _dom_tree_build_with_neighbor_provider(instr_buffer,
			initial_regions,
			_region_successors_provider,
			successor_buffer,
			allocator,
			temp_allocator);
}

static InstrIndexArray _region_predecessors_provider(const InstrBuffer* instr_buffer,
		InstrIndex region_index,
		void* user_data) {
	InstrIndexArray* predecessors = (InstrIndexArray*)user_data;
	uint16_t region_id = instr_region_id(instr_buffer, region_index);
	return predecessors[region_id];
}

CFGDominatorTree post_dom_tree_build(const InstrBuffer* instr_buffer,
		InstrIndexArray regions,
		Arena* allocator,
		Arena* temp_allocator) {
	profile_scope_start(__func__);

	ArenaRegion temp = arena_begin_temp(temp_allocator);

	uint16_t* predecessor_count = arena_alloc_array_zeroed(temp_allocator,
			uint16_t,
			instr_buffer->region_count);

	for (size_t i = 0; i < regions.count; i += 1) {
		const Instr* region_instr = instr_buffer_at(instr_buffer, regions.instr[i]);
		const Instr* last_instr = instr_buffer_at(instr_buffer, region_instr->region.last_instr);

		switch (last_instr->kind) {
		case INSTR_JUMP: {
			uint16_t target_region_id = instr_region_id(instr_buffer,
					last_instr->jump.target_region);

			predecessor_count[target_region_id] += 1;
			break;
		}
		case INSTR_BRANCH: {
			uint16_t true_region_id = instr_region_id(instr_buffer,
					last_instr->branch.true_region);

			uint16_t false_region_id = instr_region_id(instr_buffer,
					last_instr->branch.false_region);

			predecessor_count[true_region_id] += 1;
			predecessor_count[false_region_id] += 1;
			break;
		}
		case INSTR_RET:
		case INSTR_RETURN_VALUE:
			break;
		default:
			unreachable();
		}
	}

	InstrIndexArray* predecessors = arena_alloc_array(temp_allocator,
			InstrIndexArray,
			instr_buffer->region_count);

	for (size_t i = 0; i < regions.count; i += 1) {
		uint16_t region_id = instr_region_id(instr_buffer, regions.instr[i]);

		predecessors[region_id].count = 0;
		predecessors[region_id].instr = arena_alloc_array(temp_allocator,
				InstrIndex,
				predecessor_count[region_id]);
	}

	for (size_t i = 0; i < regions.count; i += 1) {
		InstrIndex current_region_index = regions.instr[i];
		const Instr* region_instr = instr_buffer_at(instr_buffer, current_region_index);
		const Instr* last_instr = instr_buffer_at(instr_buffer, region_instr->region.last_instr);

		switch (last_instr->kind) {
		case INSTR_JUMP: {
			uint16_t target_region_id = instr_region_id(instr_buffer,
					last_instr->jump.target_region);

			size_t count = predecessors[target_region_id].count;
			assert(count + 1 <= predecessor_count[target_region_id]);

			predecessors[target_region_id].instr[count] = current_region_index;
			predecessors[target_region_id].count += 1;
			break;
		}
		case INSTR_BRANCH: {
			uint16_t true_region_id = instr_region_id(instr_buffer,
					last_instr->branch.true_region);

			uint16_t false_region_id = instr_region_id(instr_buffer,
					last_instr->branch.false_region);

			{
				size_t count = predecessors[true_region_id].count;
				assert(count + 1 <= predecessor_count[true_region_id]);

				predecessors[true_region_id].instr[count] = current_region_index;
				predecessors[true_region_id].count += 1;
			}

			{
				size_t count = predecessors[false_region_id].count;
				assert(count + 1 <= predecessor_count[false_region_id]);

				predecessors[false_region_id].instr[count] = current_region_index;
				predecessors[false_region_id].count += 1;
			}

			break;
		}
		case INSTR_RET:
		case INSTR_RETURN_VALUE:
			break;
		default:
			unreachable();
		}
	}

	// Now gather all the regions ending with a `return`. These will act as a starting point for
	// post-dominator tree building process.
	InstrIndexArray final_regions;
	final_regions.count = 0;
	final_regions.instr = arena_alloc_array(temp_allocator, InstrIndex, 0);

	for (size_t i = 0; i < regions.count; i += 1) {
		InstrIndex current_region_index = regions.instr[i];
		const Instr* region_instr = instr_buffer_at(instr_buffer, current_region_index);
		const Instr* last_instr = instr_buffer_at(instr_buffer, region_instr->region.last_instr);

		if (last_instr->kind == INSTR_RET || last_instr->kind == INSTR_RETURN_VALUE) {
			arena_alloc(temp_allocator, InstrIndex);
			final_regions.instr[final_regions.count] = current_region_index;
			final_regions.count += 1;
		}
	}

	CFGDominatorTree tree = _dom_tree_build_with_neighbor_provider(instr_buffer,
			final_regions,
			_region_predecessors_provider,
			predecessors,
			allocator,
			temp_allocator);

	arena_end_temp(temp);

	profile_scope_end();
	return tree;
}

bool dom_tree_is_region_dominated_by(const CFGDominatorTree* tree,
		uint16_t dominated_region_id,
		uint16_t dominated_by_region_id) {
	const BitArray* dominated_regions = &tree->dominates[dominated_by_region_id];
	return bit_array_get(dominated_regions, dominated_region_id);
}

uint16_t dom_tree_find_control_flow_split(const CFGDominatorTree* tree,
		uint16_t region_a_id,
		uint16_t region_b_id,
		Arena* temp_allocator) {
	profile_scope_start(__func__);

	if (region_a_id == region_b_id) {
		profile_scope_end();
		return region_b_id;
	}

	ArenaRegion temp = arena_begin_temp(temp_allocator);
	BitArray visited_regions = bit_array_alloc(temp_allocator, tree->region_count);
	bit_array_clear(&visited_regions);

	// NOTE: There is no queue for `uint16_t`, so just reuse the implementation of `InstrIndex`
	InstrIndex backing_buffer[2];
	InstrQueue queue;
	instr_queue_init(&queue, backing_buffer, array_size(backing_buffer));

	instr_queue_push_back(&queue, (InstrIndex) { region_a_id });
	instr_queue_push_back(&queue, (InstrIndex) { region_b_id });

	while (queue.count) {
		InstrIndex region_id = instr_queue_pop_front(&queue);

		if (region_id.value == UINT16_MAX) {
			continue;
		}

		if (bit_array_get(&visited_regions, region_id.value)) {
			arena_end_temp(temp);
			profile_scope_end();
			return region_id.value;
		}

		bit_array_set(&visited_regions, region_id.value, true);
		instr_queue_push_back(&queue, (InstrIndex) { tree->parent[region_id.value] });
	}

	unreachable();
	profile_scope_end();
	return UINT16_MAX;
}

void dom_tree_print(const CFGDominatorTree* tree, const InstrBuffer* instr_buffer) {
	printf("dom tree:\n");
	for (uint16_t i = 0; i < instr_buffer->region_count; i += 1) {
		printf("region id=%u imm dom=%u: ",
				(uint32_t)i,
				(uint32_t)tree->parent[i]);

		for (uint16_t j = 0; j < instr_buffer->region_count; j += 1) {
			if (bit_array_get(&tree->dominates[i], j)) {
				printf("%u ", (uint32_t)j);
			}
		}
		printf("\n");
	} 
}
