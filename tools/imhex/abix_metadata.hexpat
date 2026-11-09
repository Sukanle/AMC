#pragma author AMC
#pragma description ABIX Metadata Region (manifest + desc + hash + names)
#pragma endian little
#pragma magic [ 41 42 49 58 01 00 ] @ 0x00

// ABIX Metadata Region
// --------------------
//   [ manifest ]  MetadataHeader (128 bytes)
//   [ desc     ]  type/field/function/parameter/symbol records
//   [ hash     ]  optional type-id -> type-index records
//   [ names    ]  optional NUL-terminated string table
//
// The region can be opened as a standalone `.abixmeta` image or located at the
// start of a native `.abix.metadata` section. For an embedded region, change
// `metadata_base` to the section file offset.

u64 metadata_base = 0;
u64 names_data = std::mem::read_unsigned(metadata_base + 112, 8);

struct Hash128 {
    u64 lo;
    u64 hi;
};

struct MetadataHeader {
    u32 magic;
    u16 version_major;
    u16 version_minor;
    u32 flags;
    padding[4];
    Hash128 build_id;
    Hash128 metadata_id;
    u32 hash_algorithm;
    u32 hash_version;
    u32 type_count;
    u32 field_count;
    u32 function_count;
    u32 parameter_count;
    u32 symbol_count;
    padding[4];
    u64 desc_offset;
    u64 desc_size;
    u64 hash_offset;
    u64 hash_size;
    u64 names_offset;
    u64 names_size;
};

struct MetadataTypeRecord {
    Hash128 type_id;
    Hash128 layout_hash;
    u32 flags;
    u32 kind;
    u32 size;
    u32 align;
    u32 field_begin;
    u32 field_count;
    u32 array_count;
    u32 name_offset;
    if (names_data != 0)
        char name[] @ names_data + name_offset;
};

struct MetadataFieldRecord {
    Hash128 owner_type;
    Hash128 type_id;
    u32 offset;
    u32 flags;
    u32 name_offset;
    u32 reserved;
    if (names_data != 0)
        char name[] @ names_data + name_offset;
};

struct MetadataFunctionRecord {
    Hash128 owner_type;
    Hash128 signature;
    Hash128 return_type;
    u32 calling_convention;
    u32 flags;
    u32 parameter_begin;
    u32 parameter_count;
    u32 name_offset;
    u32 reserved;
    if (names_data != 0)
        char name[] @ names_data + name_offset;
};

struct MetadataParameterRecord {
    Hash128 type_id;
    u32 flags;
    u32 name_offset;
    u64 reserved;
    if (names_data != 0)
        char name[] @ names_data + name_offset;
};

struct MetadataSymbolRecord {
    u32 name_offset;
    u32 kind;
    u32 target_index;
    u32 reserved;
    if (names_data != 0)
        char name[] @ names_data + name_offset;
};

struct MetadataHashRecord {
    Hash128 type_id;
    u32 type_index;
    u32 reserved;
};

struct MetadataExtensionHeader {
    u32 version;
    u32 count;
    u32 flags;
    u32 reserved;
};

enum MetadataExtensionKind : u32 {
    Ownership = 1,
    Error = 2,
    Target = 3,
    Lowering = 4,
    View = 5,
    Range = 6,
    Interface = 7,
    InterfaceMethod = 8,
    Implementation = 9,
    MethodBinding = 10,
    InterfaceParameter = 11,
    LayoutEncoding = 12
};

struct MetadataOwnershipRecord {
    u32 target_kind;
    u32 target_index;
    u32 mode;
    u32 release_function;
    u32 flags;
    u32 reserved;
};

struct MetadataErrorRecord {
    u32 function_index;
    u32 status_source;
    u32 status_parameter;
    u32 value_parameter;
    i64 success_value;
    u32 flags;
    u32 reserved;
    u64 domain;
    u32 condition;
    u32 reserved2;
    Hash128 error_set;
};

struct MetadataTargetRecord {
    u32 arch;
    u32 os;
    u32 target_abi;
    u32 compiler;
    u32 calling_convention;
    u32 pointer_bits;
    u32 endianness;
    u32 flags;
};

struct MetadataLayoutEncodingRecord {
    u32 version;
    u32 reserved;
};

struct MetadataLoweringRecord {
    u32 type_index;
    u32 contract_index;
    u32 kind;
    u32 status;
    u32 reason;
    u32 pointer_field;
    u32 length_field;
    u32 reserved;
};

struct MetadataViewAdapterRecord {
    u32 source_index;
    u32 target_index;
    u32 data_accessor;
    u32 size_accessor;
    u32 flags;
    u32 reserved;
};

struct MetadataRangeAdapterRecord {
    u32 source_index;
    u32 size_accessor;
    u32 at_accessor;
    u32 flags;
};

struct MetadataInterfaceRecord {
    Hash128 interface_id;
    u32 version;
    u32 method_begin;
    u32 method_count;
    u32 flags;
    u32 name_offset;
    if (names_data != 0)
        char name[] @ names_data + name_offset;
};

struct MetadataInterfaceMethodRecord {
    Hash128 interface_id;
    Hash128 signature;
    Hash128 return_type;
    u32 slot;
    u32 flags;
    u32 name_offset;
    u32 parameter_begin;
    u32 parameter_count;
    padding[4];
    if (names_data != 0)
        char name[] @ names_data + name_offset;
};

struct MetadataInterfaceParameterRecord {
    u32 type_index;
    u32 flags;
};

struct MetadataInterfaceImplementationRecord {
    Hash128 interface_id;
    Hash128 concrete_type;
    u32 binding_begin;
    u32 binding_count;
    u32 flags;
    padding[4];
};

struct MetadataMethodBindingRecord {
    Hash128 interface_id;
    Hash128 method_signature;
    Hash128 native_function;
    u32 slot;
    u32 flags;
};

// One extension entry plus the payload it addresses. `byte_offset` is relative
// to the start of the desc section.
struct MetadataExtension<auto desc_base> {
    u32 kind;
    u32 version;
    u32 count;
    u32 byte_offset;
    u32 byte_size;
    u32 reserved;

    match (kind) {
        (MetadataExtensionKind::Ownership): {
            MetadataOwnershipRecord records[count] @ desc_base + byte_offset;
        }
        (MetadataExtensionKind::Error): {
            MetadataErrorRecord records[count] @ desc_base + byte_offset;
        }
        (MetadataExtensionKind::Target): {
            MetadataTargetRecord records[count] @ desc_base + byte_offset;
        }
        (MetadataExtensionKind::Lowering): {
            MetadataLoweringRecord records[count] @ desc_base + byte_offset;
        }
        (MetadataExtensionKind::View): {
            MetadataViewAdapterRecord records[count] @ desc_base + byte_offset;
        }
        (MetadataExtensionKind::Range): {
            MetadataRangeAdapterRecord records[count] @ desc_base + byte_offset;
        }
        (MetadataExtensionKind::Interface): {
            MetadataInterfaceRecord records[count] @ desc_base + byte_offset;
        }
        (MetadataExtensionKind::InterfaceMethod): {
            MetadataInterfaceMethodRecord records[count] @ desc_base + byte_offset;
        }
        (MetadataExtensionKind::Implementation): {
            MetadataInterfaceImplementationRecord records[count] @ desc_base + byte_offset;
        }
        (MetadataExtensionKind::MethodBinding): {
            MetadataMethodBindingRecord records[count] @ desc_base + byte_offset;
        }
        (MetadataExtensionKind::InterfaceParameter): {
            MetadataInterfaceParameterRecord records[count] @ desc_base + byte_offset;
        }
        (MetadataExtensionKind::LayoutEncoding): {
            MetadataLayoutEncodingRecord records[count] @ desc_base + byte_offset;
        }
        (_): {
            u8 raw[byte_size] @ desc_base + byte_offset;
        }
    }
};

struct MetadataRegion {
    MetadataHeader header;

    u64 expected_desc_size =
        header.type_count * 64 +
        header.field_count * 48 +
        header.function_count * 72 +
        header.parameter_count * 32 +
        header.symbol_count * 16;

    MetadataTypeRecord types[header.type_count] @ metadata_base + header.desc_offset;
    MetadataFieldRecord fields[header.field_count] @ metadata_base + header.desc_offset + header.type_count * 64;
    MetadataFunctionRecord functions[header.function_count] @
        metadata_base + header.desc_offset + header.type_count * 64 + header.field_count * 48;
    MetadataParameterRecord parameters[header.parameter_count] @
        metadata_base + header.desc_offset + header.type_count * 64 + header.field_count * 48 +
        header.function_count * 72;
    MetadataSymbolRecord symbols[header.symbol_count] @
        metadata_base + header.desc_offset + header.type_count * 64 + header.field_count * 48 +
        header.function_count * 72 + header.parameter_count * 32;

    if (header.desc_size > expected_desc_size) {
        MetadataExtensionHeader extension_header @ metadata_base + header.desc_offset + expected_desc_size;
        MetadataExtension<metadata_base + header.desc_offset> extensions[extension_header.count] @
            metadata_base + header.desc_offset + expected_desc_size + 16;
    }

    if (header.hash_size > 0)
        MetadataHashRecord hash_index[header.hash_size / 24] @ metadata_base + header.hash_offset;

    if (header.names_size > 0)
        char names[header.names_size] @ metadata_base + header.names_offset;
};

MetadataRegion region @ metadata_base;
