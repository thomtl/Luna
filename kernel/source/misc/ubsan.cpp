#include <Luna/common.hpp>
#include <Luna/misc/log.hpp>

enum class TypeKinds : uint16_t {TK_INTEGER = 0x0, TK_FLOAT = 0x1, TK_UNKNOWN = 0xFFFF};

struct TypeDescriptor {
	TypeKinds kind;
	uint16_t type_info;
	char type_name[];
	void print(const char* prefix){
		::print("{} Type Descriptor: name: {}, info: {}, ", prefix, type_name, type_info);
		switch (kind)
		{
		case TypeKinds::TK_INTEGER:
			::print("Type: Integer");
			break;
		case TypeKinds::TK_FLOAT:
			::print("Type: Float");
			break;

		case TypeKinds::TK_UNKNOWN:
			::print("Type: Unknown");
			break;
		default:
			::print("Type: Undefined [{}]", static_cast<uint16_t>(kind));
			break;
		}
		::print("\n");
	}
};

struct SourceLocation {
	const char* filename;
	uint32_t line;
	uint32_t column;
	void print(const char* prefix){
		::print("{}Location Descriptor: file: {}, line: {}, column: {}\n", prefix, filename, line, column);
	}
};

struct OverflowData {
	SourceLocation loc;
	TypeDescriptor* type;
	void print(const char* prefix){
		type->print(prefix);
		loc.print(prefix);
	}
};

struct ShiftOutOfBoundsData {
	SourceLocation loc;
	TypeDescriptor* lhs;
	TypeDescriptor* rhs;
	void print(const char* prefix){
		::print("{}rhs: ", prefix);
		rhs->print("");
		::print("{}lhs: ", prefix);
		lhs->print("");
		loc.print(prefix);
	}
};

struct OutOfBoundsData {
    SourceLocation loc;
	TypeDescriptor* array;
	TypeDescriptor* index;
	void print(const char* prefix){
		::print("{} Array: ", prefix);
		array->print("");
		::print("{} Index: ", prefix);
		array->print("");
		loc.print(prefix);
	}
};

struct NonNullReturnData {
	SourceLocation attribute_loc;
	void print(const char* prefix){
		attribute_loc.print(prefix);
	}
};

struct TypeMismatchDataV1 {
	SourceLocation loc;
	TypeDescriptor* type;
	uint8_t log_alignment;
	uint8_t type_check_kind;
	void print(const char* prefix){
		loc.print(prefix);
		type->print(prefix);
		::print("{}, alignment: {}, type_check_kind: {}\n", prefix, 2 << log_alignment, type_check_kind);
	}
};

struct TypeMismatchData {
	SourceLocation loc;
	TypeDescriptor* type;
	uint64_t alignment;
	uint8_t type_check_kind;
};

struct VlaBoundData {
	SourceLocation loc;
	TypeDescriptor* type;
	void print(const char* prefix){
		loc.print(prefix);
		type->print(prefix);
	}
};

struct InvalidValueData {
	SourceLocation loc;
	TypeDescriptor* type;
	void print(const char* prefix){
		loc.print(prefix);
		type->print(prefix);
	}
};

struct UnreachableData {
	SourceLocation loc;
	void print(const char* prefix){
		loc.print(prefix);
	}
};

struct NonnullArgData {
    SourceLocation loc;
	void print(const char* prefix){
		loc.print(prefix);
	}
};


constexpr const char* builtin_check_kind[] = {
    "CTZ passed zero",
    "CLZ passed zero"
};

struct InvalidBuiltinKind {
    SourceLocation loc;
    uint8_t kind;
    void print(const char* prefix) {
        loc.print(prefix);
        ::print(", Kind: {}", builtin_check_kind[kind]);
    }
};

enum class TypeNames : int { 	add_overflow = 0,
    						 	sub_overflow,
    							mul_overflow,
   								divrem_overflow,
    							negate_overflow,
    							shift_out_of_bounds,
    							out_of_bounds,
    							nonnull_return,
    							type_mismatch_v1,
    							vla_bound_not_positive,
    							load_invalid_value,
    							builtin_unreachable,
    							nonnull_arg,
    						  	pointer_overflow,
    						  	type_mismatch };

/*static const char* type_strs[] = {
    "add_overflow",
    "sub_overflow",
    "mul_overflow",
	"divrem_overflow",
	"negate_overflow",
    "shift_out_of_bounds",
	"out_of_bounds",
    "nonnull_return",
	"type_mismatch_v1",
    "vla_bound_not_positive",
	"load_invalid_value",
    "builtin_unreachable",
	"nonnull_arg",
    "pointer_overflow",
	"type_mismatch"
};*/

constexpr const char* type_check_kind[] = {
    "load of",
    "store to",
	"reference binding to",
    "member access within",
	"member call on",
	"constructor call on",
    "downcast of",
    "downcast of",
    "upcast of",
	"cast to virtual base of",
};

extern "C" void __ubsan_handle_add_overflow(OverflowData* data, uintptr_t lhs, uintptr_t rhs) {
	print("ubsan: Add overflow\n");
	data->print("	");
	print("	lhs: {}, rhs: {}\n", lhs, rhs);
}

extern "C" void __ubsan_handle_sub_overflow(OverflowData* data, uintptr_t lhs, uintptr_t rhs) {
	print("ubsan: Sub overflow\n");
	data->print("	");
	print("	lhs: {}, rhs: {}\n", lhs, rhs);
}

extern "C" void __ubsan_handle_pointer_overflow(OverflowData* data, uintptr_t lhs, uintptr_t rhs) {
	print("ubsan: Pointer overflow\n");
	data->print("	");
	print("	lhs: {}, rhs: {}\n", lhs, rhs);
}
 
extern "C" void __ubsan_handle_mul_overflow(OverflowData* data, uintptr_t lhs, uintptr_t rhs) {
	print("ubsan: Mul overflow\n");
	data->print("	");
	print("	lhs: {}, rhs: {}\n", lhs, rhs);
}
 
extern "C" void __ubsan_handle_divrem_overflow(OverflowData* data, uintptr_t lhs, uintptr_t rhs) {
	print("ubsan: Divide Remainder overflow\n");
	data->print("	");
	print("	lhs: {}, rhs: {}\n", lhs, rhs);
}
 
extern "C" void __ubsan_handle_negate_overflow(OverflowData* data, uintptr_t old) {
	print("ubsan: Negate overflow\n");
	data->print("	");
	print("	lhs: {}\n", old);
}
 
extern "C" void __ubsan_handle_shift_out_of_bounds(ShiftOutOfBoundsData* data, uintptr_t lhs, uintptr_t rhs) {
	print("ubsan: Shift out of bounds\n");
	data->print("	");
	print("	 lhs: {}, rhs: {}\n", lhs, rhs);
}
 
extern "C" void __ubsan_handle_out_of_bounds(OutOfBoundsData* data, uintptr_t index) {
	print("ubsan: Out of bounds\n");
	data->print("	");
	print("	index: {}\n", index);
}
 
extern "C" void __ubsan_handle_nonnull_return(NonNullReturnData* data, SourceLocation* loc) {
	print("ubsan: Nonnull return\n");
	data->print("	");
	print("	Return loc: ");
	loc->print("	");
	print("\n");
}

extern "C" void __ubsan_handle_type_mismatch_v1(TypeMismatchDataV1* data, uintptr_t ptr) {
	print("ubsan: Type Mismatch\n");
	if(!ptr) {
        print("       Null pointer access\n");
    } else if (ptr &  ((1 << data->log_alignment) - 1)) {
    	print("       Unaligned memory access\n");
		print("       ptr: {:#x}\n", ptr);
    } else {
    	print("       Insufficient size\n");
		print("       {} address {:#x} with insufficient space for object of type\n", type_check_kind[data->type_check_kind], ptr);
		data->type->print("       ");
	}
	data->loc.print("       ");
	print("       aligned at: {}\n", 1 << data->log_alignment);
}
 
extern "C" void __ubsan_handle_vla_bound_not_positive(VlaBoundData* data, uintptr_t bound) {
	print("ubsan: vla bound not positive\n");
	data->print("       ");
	print("       bound: {}\n", bound);
}
 
extern "C" void __ubsan_handle_load_invalid_value(InvalidValueData* data, uintptr_t val) {
	print("ubsan: load invalid value\n");
	data->print("	");
	print("	value: {}\n", val);
}
 
extern "C" void __ubsan_handle_builtin_unreachable(UnreachableData* data) {
	print("ubsan: builtin unreachable\n");
	data->print("	");
}
 
extern "C" void __ubsan_handle_nonnull_arg(NonnullArgData* data) {
	print("ubsan: nonnull arg\n");
	data->print("	");
}
 
extern "C" void __ubsan_handle_type_mismatch(TypeMismatchData* data, uintptr_t ptr) {
	print("ubsan: Type Mismatch\n");
	if(!ptr) {
        print("	Null pointer access\n");
    } else if (ptr & (data->alignment - 1)) {
    	print("	Unaligned memory access\n");
		print("	ptr: {:#x}\n", ptr);
    } else {
    	print("	Insufficient size\n");
		print("	{} address {:#x} with insufficient space for object of type ↓\n", type_check_kind[data->type_check_kind], ptr);
		data->type->print("	");
	}
	data->loc.print("	");
	print("aligned at: {}\n", data->alignment);
}

extern "C" void __ubsan_handle_invalid_builtin(InvalidBuiltinKind* data) {
    print("ubsan: Invalid builtin\n");
    data->print("    ");
}