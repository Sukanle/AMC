#pragma author AMC
#pragma description ABIX v4 artifact (.abix) produced by AMC
#pragma endian little
#pragma magic [ 41 42 49 58 04 00 ] @ 0x00

// ABIX artifact container
// ----------------------
//   0x00  header
//   0x14  section directory (section_count * 24 bytes)
//   ...   section payloads, addressed by directory.offset
//
// All integers are little-endian. A section payload is placed by its directory
// entry; the directory itself is always sequential.

struct Hash128 {
    u64 lo;
    u64 hi;
};

enum SectionId : u32 {
    Strings = 1,
    Identity = 2,
    Types = 3,
    Fields = 4,
    Functions = 5,
    Parameters = 6,
    Symbols = 7,
    HashDescriptor = 8,
    HashRecords = 9,
    Compatibility = 10,
    Maps = 11,
    MapOperations = 12,
    Target = 13,
    Sources = 14,
    Owned = 15,
    Contracts = 16,
    Errors = 17,
    Platform = 18,
    Lowering = 19,
    Pointees = 20,
    ViewAdapters = 21,
    RangeAdapters = 22,
    Instantiations = 23,
    Interfaces = 24,
    InterfaceMethods = 25,
    Implementations = 26,
    MethodBindings = 27,
    LayoutEncoding = 28,
    ErrorSets = 29,
    ErrorCodes = 30
};

enum TypeKind : u32 {
    Primitive = 0,
    Enumeration = 1,
    Record = 2,
    Pointer = 3,
    Array = 4,
    Function = 5,
    Namespace = 6,
    Alias = 7
};

enum SymbolKind : u32 {
    Type = 0,
    Field = 1,
    Function = 2
};

struct StringTable {
    u32 count;
    u32 byte_size;
    char data[byte_size];
};

// Section 1: string table payload header. `strings_data` below points at the
// first byte after these two words.
fn section_offset(u32 wanted) {
    u32 count = std::mem::read_unsigned(0x0C, 4);
    u32 directory = std::mem::read_unsigned(0x10, 4);
    for (u32 i = 0, i < count, i = i + 1) {
        u32 base = directory + i * 24;
        if (std::mem::read_unsigned(base, 4) == wanted)
            return std::mem::read_unsigned(base + 4, 4);
    }
    return 0;
}

u64 strings_data = section_offset(1) + 8;

struct AbiIdentity {
    u32 arch;
    u32 os;
    u32 compiler;
    u32 calling_convention;
    u32 reserved;
    Hash128 abi_hash;
    u32 package_name_offset;
    u32 package_name_length;
    u32 package_version_offset;
    u32 package_version_length;
    if (package_name_length > 0)
        char package_name[package_name_length] @ strings_data + package_name_offset;
    if (package_version_length > 0)
        char package_version[package_version_length] @ strings_data + package_version_offset;
};

struct TargetRecord {
    u32 arch;
    u32 os;
    u32 target_abi;
};

struct TypeRecord {
    Hash128 type_id;
    Hash128 layout_hash;
    u32 name_offset;
    u32 name_length;
    TypeKind kind;
    u32 flags;
    u32 size;
    u32 align;
    u32 field_begin;
    u32 field_count;
    u32 array_count;
    u32 primitive_abi;
    if (name_length > 0)
        char name[name_length] @ strings_data + name_offset;
};

struct FieldRecord {
    Hash128 owner_type;
    u32 name_offset;
    u32 name_length;
    Hash128 type_id;
    u32 offset;
    u32 flags;
    if (name_length > 0)
        char name[name_length] @ strings_data + name_offset;
};

struct FunctionRecord {
    Hash128 owner_type;
    u32 name_offset;
    u32 name_length;
    Hash128 signature;
    Hash128 return_type;
    u32 parameter_begin;
    u32 parameter_count;
    u32 calling_convention;
    u32 flags;
    if (name_length > 0)
        char name[name_length] @ strings_data + name_offset;
};

struct ParameterRecord {
    u32 name_offset;
    u32 name_length;
    Hash128 type_id;
    u32 flags;
    if (name_length > 0)
        char name[name_length] @ strings_data + name_offset;
};

struct SymbolRecord {
    u32 name_offset;
    u32 name_length;
    SymbolKind kind;
    u32 target_index;
    if (name_length > 0)
        char name[name_length] @ strings_data + name_offset;
};

struct HashDescriptor {
    u16 algorithm;
    u16 version;
    u32 flags;
    u32 reserved0;
    u32 reserved1;
};

struct HashRecord {
    u32 kind;
    u32 target_kind;
    u32 target_index;
    u32 flags;
    Hash128 value;
};

struct CompatibilityRecord {
    Hash128 source_type;
    Hash128 target_type;
    u32 kind;
    u32 map_index;
    u32 flags;
};

struct MapRecord {
    Hash128 source_type;
    Hash128 target_type;
    u32 source_name_offset;
    u32 source_name_length;
    u32 target_name_offset;
    u32 target_name_length;
    u32 operation_begin;
    u32 operation_count;
    u32 flags;
    if (source_name_length > 0)
        char source_name[source_name_length] @ strings_data + source_name_offset;
    if (target_name_length > 0)
        char target_name[target_name_length] @ strings_data + target_name_offset;
};

struct MapOperationRecord {
    u8 opcode;
    u8 reserved0;
    u16 reserved1;
    u32 source_field;
    u32 target_field;
    u32 source_offset;
    u32 target_offset;
    u32 byte_count;
    Hash128 auxiliary;
};

struct SourceRecord {
    Hash128 type_id;
    u32 file_offset;
    u32 line;
    u32 column;
    u32 reserved;
};

struct OwnedRecord {
    Hash128 type_id;
    Hash128 create;
    Hash128 destroy;
    u32 flags;
};

struct ContractRecord {
    u32 target;
    u32 target_index;
    u32 mode;
    u32 flags;
    Hash128 release;
};

struct ErrorRecord {
    u32 function_index;
    u32 status_source;
    u32 status_parameter;
    i64 success_value;
    u32 value_parameter;
    u32 flags;
    u64 domain;
    u32 condition;
    Hash128 error_set;
};

struct PlatformRecord {
    u32 pointer_bits;
    u32 endianness;
};

struct LoweringRecord {
    Hash128 type_id;
    Hash128 contract_type;
    u32 kind;
    u32 status;
    u32 reason;
    u32 pointer_field;
    u32 length_field;
};

struct PointeeRecord {
    Hash128 pointer;
    Hash128 pointee;
    u32 flags;
};

struct ViewAdapterRecord {
    Hash128 source_type;
    Hash128 target_type;
    Hash128 data_accessor;
    Hash128 size_accessor;
};

struct RangeAdapterRecord {
    Hash128 source_type;
    Hash128 size_accessor;
    Hash128 at_accessor;
};

struct InstantiationRecord {
    Hash128 request;
    Hash128 type;
};

struct InterfaceRecord {
    Hash128 id;
    u32 name_offset;
    u32 name_length;
    u32 version;
    u32 method_begin;
    u32 method_count;
    u32 flags;
    if (name_length > 0)
        char name[name_length] @ strings_data + name_offset;
};

struct InterfaceMethodRecord {
    Hash128 interface_id;
    u32 name_offset;
    u32 name_length;
    Hash128 signature;
    Hash128 return_type;
    u32 parameter_begin;
    u32 parameter_count;
    u32 slot;
    u32 flags;
    if (name_length > 0)
        char name[name_length] @ strings_data + name_offset;
};

struct InterfaceParameterRecord {
    u32 type_index;
    u32 flags;
};

struct ImplementationRecord {
    Hash128 interface_id;
    Hash128 concrete_type;
    u32 binding_begin;
    u32 binding_count;
    u32 flags;
};

struct MethodBindingRecord {
    Hash128 interface_id;
    Hash128 method_signature;
    Hash128 native_function;
    u32 slot;
    u32 flags;
};

struct LayoutEncodingRecord {
    u32 version;
};

struct ErrorSetRecord {
    Hash128 id;
    u32 name_offset;
    u32 name_length;
    u64 domain;
    u32 condition;
    u32 flags;
    u32 code_begin;
    u32 code_count;
    if (name_length > 0)
        char name[name_length] @ strings_data + name_offset;
};

struct ErrorCodeRecord {
    Hash128 set_id;
    u32 name_offset;
    u32 name_length;
    u32 value;
    u32 condition;
    u32 flags;
    if (name_length > 0)
        char name[name_length] @ strings_data + name_offset;
};

struct DirectoryEntry {
    SectionId id;
    u32 offset;
    u32 byte_length;
    u32 count;
    u32 entry_size;
    u32 flags;

    match (id) {
        (SectionId::Strings): {
            StringTable strings @ offset;
        }
        (SectionId::Identity): {
            AbiIdentity identity @ offset;
        }
        (SectionId::Types): {
            TypeRecord records[count] @ offset;
        }
        (SectionId::Fields): {
            FieldRecord records[count] @ offset;
        }
        (SectionId::Functions): {
            FunctionRecord records[count] @ offset;
        }
        (SectionId::Parameters): {
            ParameterRecord records[count] @ offset;
        }
        (SectionId::Symbols): {
            SymbolRecord records[count] @ offset;
        }
        (SectionId::HashDescriptor): {
            HashDescriptor descriptor @ offset;
        }
        (SectionId::HashRecords): {
            HashRecord records[count] @ offset;
        }
        (SectionId::Compatibility): {
            CompatibilityRecord records[count] @ offset;
        }
        (SectionId::Maps): {
            MapRecord records[count] @ offset;
        }
        (SectionId::MapOperations): {
            MapOperationRecord records[count] @ offset;
        }
        (SectionId::Target): {
            TargetRecord target @ offset;
        }
        (SectionId::Sources): {
            SourceRecord records[count] @ offset;
        }
        (SectionId::Owned): {
            OwnedRecord records[count] @ offset;
        }
        (SectionId::Contracts): {
            ContractRecord records[count] @ offset;
        }
        (SectionId::Errors): {
            ErrorRecord records[count] @ offset;
        }
        (SectionId::Platform): {
            PlatformRecord platform @ offset;
        }
        (SectionId::Lowering): {
            LoweringRecord records[count] @ offset;
        }
        (SectionId::Pointees): {
            PointeeRecord records[count] @ offset;
        }
        (SectionId::ViewAdapters): {
            ViewAdapterRecord records[count] @ offset;
        }
        (SectionId::RangeAdapters): {
            RangeAdapterRecord records[count] @ offset;
        }
        (SectionId::Instantiations): {
            InstantiationRecord records[count] @ offset;
        }
        (SectionId::Interfaces): {
            InterfaceRecord records[count] @ offset;
        }
        (SectionId::InterfaceMethods): {
            InterfaceMethodRecord records[count] @ offset;
        }
        (SectionId::Implementations): {
            ImplementationRecord records[count] @ offset;
        }
        (SectionId::MethodBindings): {
            MethodBindingRecord records[count] @ offset;
        }
        (SectionId::LayoutEncoding): {
            LayoutEncodingRecord records[count] @ offset;
        }
        (SectionId::ErrorSets): {
            ErrorSetRecord records[count] @ offset;
        }
        (SectionId::ErrorCodes): {
            ErrorCodeRecord records[count] @ offset;
        }
        (_): {
            u8 raw[byte_length] @ offset;
        }
    }
};

struct AbixArtifact {
    char magic[4];
    u16 version;
    u16 hash_algorithm;
    u32 flags;
    u32 section_count;
    u32 directory_offset;
    DirectoryEntry sections[section_count] @ directory_offset;
};

AbixArtifact artifact @ 0x00;
