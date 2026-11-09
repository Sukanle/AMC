// Runtime reader for the ABIX Metadata Region embedded above.

pub mod metadata {
    pub const MAGIC: u32 = 0x5849_4241; // "ABIX"
    pub const MANIFEST_SIZE: usize = 128;
    const TYPE_RECORD_SIZE: usize = 64;
    const FIELD_RECORD_SIZE: usize = 48;
    const FUNCTION_RECORD_SIZE: usize = 72;
    const PARAMETER_RECORD_SIZE: usize = 32;
    const SYMBOL_RECORD_SIZE: usize = 16;
    const OWNERSHIP_RECORD_SIZE: usize = 24;
    const ERROR_RECORD_SIZE: usize = 64;
    const LOWERING_RECORD_SIZE: usize = 32;
    const INTERFACE_RECORD_SIZE: usize = 40;
    const INTERFACE_METHOD_RECORD_SIZE: usize = 72;
    const INTERFACE_PARAMETER_RECORD_SIZE: usize = 8;
    const INTERFACE_IMPLEMENTATION_RECORD_SIZE: usize = 48;
    const METHOD_BINDING_RECORD_SIZE: usize = 56;
    const EXTENSION_HEADER_SIZE: usize = 16;
    const EXTENSION_ENTRY_SIZE: usize = 24;

    // Trailing extension directory: version of the directory encoding, and the
    // kind ids that name each contract type (not the record shape).
    pub const EXTENSION_VERSION: u32 = 1;
    pub const EXTENSION_OWNERSHIP: u32 = 1;
    pub const EXTENSION_ERROR: u32 = 2;
    pub const EXTENSION_TARGET: u32 = 3;
    pub const EXTENSION_LOWERING: u32 = 4;
    pub const EXTENSION_VIEW: u32 = 5;
    pub const EXTENSION_RANGE: u32 = 6;
    // Non-intrusive interface contract family.
    pub const EXTENSION_INTERFACE: u32 = 7;
    pub const EXTENSION_INTERFACE_METHOD: u32 = 8;
    pub const EXTENSION_IMPLEMENTATION: u32 = 9;
    pub const EXTENSION_METHOD_BINDING: u32 = 10;
    pub const EXTENSION_INTERFACE_PARAMETER: u32 = 11;

    // ABIX-STD lowering status / reason codes.
    pub const LOWERING_DIRECT: u32 = 0;
    pub const LOWERING_NEEDS_ADAPTER: u32 = 1;
    pub const LOWERING_UNSUPPORTED: u32 = 2;
    pub const LOWERING_REASON_OK: u32 = 0;
    pub const LOWERING_REASON_FIELD_COUNT: u32 = 2;
    pub const LOWERING_REASON_MISSING_DATA: u32 = 3;
    pub const LOWERING_REASON_ELEMENT_MISMATCH: u32 = 5;
    pub const LOWERING_REASON_MUTABILITY_MISMATCH: u32 = 6;
    pub const LOWERING_REASON_LENGTH_TYPE: u32 = 7;
    pub const LOWERING_REASON_LAYOUT: u32 = 8;

    // Target verification outcomes.
    pub const TARGET_COMPATIBLE: u32 = 0;
    pub const TARGET_INCOMPATIBLE: u32 = 1;
    pub const TARGET_UNVERIFIABLE: u32 = 2;

    // Ownership modes of an ABI interaction (parameter or return value).
    pub const OWNERSHIP_UNSPECIFIED: u32 = 0;
    pub const OWNERSHIP_BORROWED: u32 = 1;
    pub const OWNERSHIP_OWNED: u32 = 2;
    pub const OWNERSHIP_COPY: u32 = 3;

    #[derive(Clone, Copy, PartialEq, Eq)]
    pub struct Hash128 {
        pub lo: u64,
        pub hi: u64,
    }

    #[derive(Clone, Copy)]
    pub struct Header {
        pub flags: u32,
        pub build_id: Hash128,
        pub metadata_id: Hash128,
        pub type_count: u32,
        pub field_count: u32,
        pub function_count: u32,
        pub parameter_count: u32,
        pub symbol_count: u32,
        pub desc_offset: u64,
        pub desc_size: u64,
        pub names_offset: u64,
        pub names_size: u64,
    }

    #[derive(Clone, Copy)]
    pub struct TypeRecord {
        pub id: Hash128,
        pub layout_hash: Hash128,
        pub flags: u32,
        pub kind: u32,
        pub size: u32,
        pub align: u32,
        pub field_begin: u32,
        pub field_count: u32,
        pub array_count: u32,
        pub name_offset: u32,
    }

    #[derive(Clone, Copy)]
    pub struct FunctionRecord {
        pub owner_type: Hash128,
        pub signature: Hash128,
        pub return_type: Hash128,
        pub calling_convention: u32,
        pub parameter_begin: u32,
        pub parameter_count: u32,
        pub name_offset: u32,
    }

    #[derive(Clone, Copy)]
    pub struct FieldRecord {
        pub owner_type: Hash128,
        pub type_id: Hash128,
        pub offset: u32,
        pub flags: u32,
        pub name_offset: u32,
    }

    #[derive(Clone, Copy)]
    pub struct ParameterRecord {
        pub type_id: Hash128,
        pub flags: u32,
        pub name_offset: u32,
    }

    /// One ownership contract: the mode of a parameter or return value, plus
    /// the function-table index that releases it when the mode is `owned`
    /// (`release_function == u32::MAX` when absent).
    #[derive(Clone, Copy)]
    pub struct OwnershipRecord {
        pub target_kind: u32,  // 0 = parameter, 1 = return value
        pub target_index: u32, // global parameter index, or function index
        pub mode: u32,         // OWNERSHIP_*
        pub release_function: u32,
        pub flags: u32,
    }

    /// One error-boundary contract: how a function reports success and where
    /// the result value is carried.
    #[derive(Clone, Copy)]
    pub struct ErrorRecord {
        pub function_index: u32,
        pub status_source: u32,    // 0 = return value, 1 = parameter
        pub status_parameter: u32, // local parameter index, or u32::MAX
        pub value_parameter: u32,  // local parameter index, or u32::MAX
        pub success_value: i64,
        pub flags: u32,
        pub domain: u64,        // v2 error identity domain
        pub condition: i32,     // EPCD GenericCondition
        pub error_set: Hash128, // optional error-set identity (zero = none)
    }

    /// One entry in the trailing extension directory.
    #[derive(Clone, Copy)]
    pub struct ExtensionEntry {
        pub kind: u32,
        pub version: u32,
        pub count: u32,
        pub byte_offset: u32, // relative to the start of the desc section
        pub byte_size: u32,
    }

    /// One ABIX-STD lowering result (types referenced by table index).
    #[derive(Clone, Copy)]
    pub struct LoweringRecord {
        pub type_index: u32,
        pub contract_index: u32,
        pub kind: u32,
        pub status: u32, // LOWERING_*
        pub reason: u32, // LOWERING_REASON_*
        pub pointer_field: u32,
        pub length_field: u32,
    }

    /// A container accessor adapter (view or range); functions by table index.
    #[derive(Clone, Copy)]
    pub struct ViewAdapterRecord {
        pub source_index: u32,
        pub target_index: u32,
        pub data_accessor: u32,
        pub size_accessor: u32,
        pub semantics: u32, // 1 = borrowed, 2 = owned, 4 = copy
    }
    #[derive(Clone, Copy)]
    pub struct RangeAdapterRecord {
        pub source_index: u32,
        pub size_accessor: u32,
        pub at_accessor: u32,
        pub semantics: u32,
    }

    /// A named, versioned interface contract. Identity is the content hash
    /// `id`; method identity is (`id`, slot), never a compiler vtable slot.
    #[derive(Clone, Copy)]
    pub struct InterfaceRecord {
        pub id: Hash128,
        pub version: u32,
        pub method_begin: u32,
        pub method_count: u32,
        pub flags: u32,
        pub name_offset: u32,
    }
    /// One interface method. Parameters live in the interface-parameter table
    /// addressed by `parameter_begin` / `parameter_count`.
    #[derive(Clone, Copy)]
    pub struct InterfaceMethodRecord {
        pub interface_id: Hash128,
        pub signature: Hash128,
        pub return_type: Hash128,
        pub slot: u32,
        pub flags: u32,
        pub parameter_begin: u32,
        pub parameter_count: u32,
    }
    /// A concrete type's implementation of an interface.
    #[derive(Clone, Copy)]
    pub struct InterfaceImplementationRecord {
        pub interface_id: Hash128,
        pub concrete_type: Hash128,
        pub binding_begin: u32,
        pub binding_count: u32,
        pub flags: u32,
    }
    /// How one interface method is reached on one concrete type. The adapter is
    /// named by its signature; no private vtable is exposed.
    #[derive(Clone, Copy)]
    pub struct MethodBindingRecord {
        pub interface_id: Hash128,
        pub method_signature: Hash128,
        pub native_function: Hash128,
        pub slot: u32,
        pub flags: u32,
    }

    /// Target platform identity recorded in the region.
    #[derive(Clone, Copy)]
    pub struct TargetDescription {
        pub arch: u32,
        pub os: u32,
        pub target_abi: u32,
        pub compiler: u32,
        pub calling_convention: u32,
        pub pointer_bits: u32,
        pub endianness: u32, // 0 unknown, 1 little, 2 big
    }

    /// What the caller requires of the target. 0 means "do not check".
    #[derive(Clone, Copy, Default)]
    pub struct TargetExpectation {
        pub arch: u32,
        pub os: u32,
        pub target_abi: u32,
        pub pointer_bits: u32,
        pub endianness: u32,
        pub calling_convention: u32,
    }

    /// Per-dimension ABI verification result (each is TARGET_*).
    #[derive(Clone, Copy)]
    pub struct AbiVerification {
        pub platform: u32,
        pub layout: u32,
        pub calling_convention: u32,
    }

    fn u32_at(b: &[u8], o: usize) -> u32 {
        u32::from_le_bytes([b[o], b[o + 1], b[o + 2], b[o + 3]])
    }
    fn u64_at(b: &[u8], o: usize) -> u64 {
        u64::from_le_bytes([
            b[o],
            b[o + 1],
            b[o + 2],
            b[o + 3],
            b[o + 4],
            b[o + 5],
            b[o + 6],
            b[o + 7],
        ])
    }
    fn hash_at(b: &[u8], o: usize) -> Hash128 {
        Hash128 {
            lo: u64_at(b, o),
            hi: u64_at(b, o + 8),
        }
    }

    pub struct Region<'a> {
        data: &'a [u8],
        pub header: Header,
    }

    impl<'a> Region<'a> {
        pub fn open(data: &'a [u8]) -> Option<Region<'a>> {
            if data.len() < MANIFEST_SIZE || u32_at(data, 0) != MAGIC {
                return None;
            }
            let header = Header {
                flags: u32_at(data, 8),
                build_id: hash_at(data, 16),
                metadata_id: hash_at(data, 32),
                type_count: u32_at(data, 56),
                field_count: u32_at(data, 60),
                function_count: u32_at(data, 64),
                parameter_count: u32_at(data, 68),
                symbol_count: u32_at(data, 72),
                desc_offset: u64_at(data, 80),
                desc_size: u64_at(data, 88),
                names_offset: u64_at(data, 112),
                names_size: u64_at(data, 120),
            };
            if header.desc_offset.checked_add(header.desc_size)? > data.len() as u64 {
                return None;
            }
            Some(Region { data, header })
        }

        fn desc(&self) -> &'a [u8] {
            let begin = self.header.desc_offset as usize;
            let end = begin + self.header.desc_size as usize;
            &self.data[begin..end]
        }

        pub fn type_at(&self, index: u32) -> Option<TypeRecord> {
            if index >= self.header.type_count {
                return None;
            }
            let d = self.desc();
            let base = index as usize * TYPE_RECORD_SIZE;
            if base + TYPE_RECORD_SIZE > d.len() {
                return None;
            }
            Some(TypeRecord {
                id: hash_at(d, base),
                layout_hash: hash_at(d, base + 16),
                flags: u32_at(d, base + 32),
                kind: u32_at(d, base + 36),
                size: u32_at(d, base + 40),
                align: u32_at(d, base + 44),
                field_begin: u32_at(d, base + 48),
                field_count: u32_at(d, base + 52),
                array_count: u32_at(d, base + 56),
                name_offset: u32_at(d, base + 60),
            })
        }

        pub fn function_at(&self, index: u32) -> Option<FunctionRecord> {
            if index >= self.header.function_count {
                return None;
            }
            let d = self.desc();
            let base = self.header.type_count as usize * TYPE_RECORD_SIZE
                + self.header.field_count as usize * FIELD_RECORD_SIZE
                + index as usize * FUNCTION_RECORD_SIZE;
            if base + FUNCTION_RECORD_SIZE > d.len() {
                return None;
            }
            Some(FunctionRecord {
                owner_type: hash_at(d, base),
                signature: hash_at(d, base + 16),
                return_type: hash_at(d, base + 32),
                calling_convention: u32_at(d, base + 48),
                parameter_begin: u32_at(d, base + 56),
                parameter_count: u32_at(d, base + 60),
                name_offset: u32_at(d, base + 64),
            })
        }

        pub fn field_at(&self, index: u32) -> Option<FieldRecord> {
            if index >= self.header.field_count {
                return None;
            }
            let d = self.desc();
            let base = self.header.type_count as usize * TYPE_RECORD_SIZE + index as usize * FIELD_RECORD_SIZE;
            if base + FIELD_RECORD_SIZE > d.len() {
                return None;
            }
            Some(FieldRecord {
                owner_type: hash_at(d, base),
                type_id: hash_at(d, base + 16),
                offset: u32_at(d, base + 32),
                flags: u32_at(d, base + 36),
                name_offset: u32_at(d, base + 40),
            })
        }

        pub fn parameter_at(&self, index: u32) -> Option<ParameterRecord> {
            if index >= self.header.parameter_count {
                return None;
            }
            let d = self.desc();
            let base = self.header.type_count as usize * TYPE_RECORD_SIZE
                + self.header.field_count as usize * FIELD_RECORD_SIZE
                + self.header.function_count as usize * FUNCTION_RECORD_SIZE
                + index as usize * PARAMETER_RECORD_SIZE;
            if base + PARAMETER_RECORD_SIZE > d.len() {
                return None;
            }
            Some(ParameterRecord {
                type_id: hash_at(d, base),
                flags: u32_at(d, base + 16),
                name_offset: u32_at(d, base + 20),
            })
        }

        fn known_desc_size(&self) -> usize {
            self.header.type_count as usize * TYPE_RECORD_SIZE
                + self.header.field_count as usize * FIELD_RECORD_SIZE
                + self.header.function_count as usize * FUNCTION_RECORD_SIZE
                + self.header.parameter_count as usize * PARAMETER_RECORD_SIZE
                + self.header.symbol_count as usize * SYMBOL_RECORD_SIZE
        }

        /// Locate an extension payload by kind. `None` when the region has no
        /// extension directory, or no extension of that kind. Bounds are checked
        /// so a malformed directory yields `None` rather than a bad read.
        pub fn extension(&self, kind: u32) -> Option<ExtensionEntry> {
            let known = self.known_desc_size();
            let desc_size = self.header.desc_size as usize;
            if desc_size < known + EXTENSION_HEADER_SIZE {
                return None;
            }
            let d = self.desc();
            if u32_at(d, known) != EXTENSION_VERSION {
                return None;
            }
            let count = u32_at(d, known + 4) as usize;
            let entries_begin = known + EXTENSION_HEADER_SIZE;
            if entries_begin + count * EXTENSION_ENTRY_SIZE > d.len() {
                return None;
            }
            for i in 0..count {
                let base = entries_begin + i * EXTENSION_ENTRY_SIZE;
                let entry = ExtensionEntry {
                    kind: u32_at(d, base),
                    version: u32_at(d, base + 4),
                    count: u32_at(d, base + 8),
                    byte_offset: u32_at(d, base + 12),
                    byte_size: u32_at(d, base + 16),
                };
                if entry.kind == kind {
                    return Some(entry);
                }
            }
            None
        }

        /// Number of declared ownership contracts; 0 when absent (every
        /// interaction is then `unspecified`).
        pub fn ownership_count(&self) -> u32 {
            self.extension(EXTENSION_OWNERSHIP).map(|e| e.count).unwrap_or(0)
        }

        pub fn ownership_at(&self, index: u32) -> Option<OwnershipRecord> {
            let ext = self.extension(EXTENSION_OWNERSHIP)?;
            if index >= ext.count {
                return None;
            }
            let d = self.desc();
            let base = ext.byte_offset as usize + index as usize * OWNERSHIP_RECORD_SIZE;
            if base + OWNERSHIP_RECORD_SIZE > d.len() {
                return None;
            }
            Some(OwnershipRecord {
                target_kind: u32_at(d, base),
                target_index: u32_at(d, base + 4),
                mode: u32_at(d, base + 8),
                release_function: u32_at(d, base + 12),
                flags: u32_at(d, base + 16),
            })
        }

        pub fn find_ownership(&self, target_kind: u32, target_index: u32) -> Option<OwnershipRecord> {
            (0..self.ownership_count())
                .filter_map(|i| self.ownership_at(i))
                .find(|r| r.target_kind == target_kind && r.target_index == target_index)
        }

        pub fn error_count(&self) -> u32 {
            self.extension(EXTENSION_ERROR).map(|e| e.count).unwrap_or(0)
        }

        pub fn error_at(&self, index: u32) -> Option<ErrorRecord> {
            let ext = self.extension(EXTENSION_ERROR)?;
            if index >= ext.count {
                return None;
            }
            let d = self.desc();
            let base = ext.byte_offset as usize + index as usize * ERROR_RECORD_SIZE;
            if base + ERROR_RECORD_SIZE > d.len() {
                return None;
            }
            Some(ErrorRecord {
                function_index: u32_at(d, base),
                status_source: u32_at(d, base + 4),
                status_parameter: u32_at(d, base + 8),
                value_parameter: u32_at(d, base + 12),
                success_value: u64_at(d, base + 16) as i64,
                flags: u32_at(d, base + 24),
                domain: u64_at(d, base + 32),
                condition: u32_at(d, base + 40) as i32,
                error_set: hash_at(d, base + 48),
            })
        }

        pub fn find_error(&self, function_index: u32) -> Option<ErrorRecord> {
            (0..self.error_count())
                .filter_map(|i| self.error_at(i))
                .find(|r| r.function_index == function_index)
        }

        pub fn lowering_count(&self) -> u32 {
            self.extension(EXTENSION_LOWERING).map(|e| e.count).unwrap_or(0)
        }

        pub fn lowering_at(&self, index: u32) -> Option<LoweringRecord> {
            let ext = self.extension(EXTENSION_LOWERING)?;
            if index >= ext.count {
                return None;
            }
            let d = self.desc();
            let base = ext.byte_offset as usize + index as usize * LOWERING_RECORD_SIZE;
            if base + LOWERING_RECORD_SIZE > d.len() {
                return None;
            }
            Some(LoweringRecord {
                type_index: u32_at(d, base),
                contract_index: u32_at(d, base + 4),
                kind: u32_at(d, base + 8),
                status: u32_at(d, base + 12),
                reason: u32_at(d, base + 16),
                pointer_field: u32_at(d, base + 20),
                length_field: u32_at(d, base + 24),
            })
        }

        pub fn find_lowering(&self, type_index: u32) -> Option<LoweringRecord> {
            (0..self.lowering_count())
                .filter_map(|i| self.lowering_at(i))
                .find(|r| r.type_index == type_index)
        }

        pub fn view_adapter_count(&self) -> u32 {
            self.extension(EXTENSION_VIEW).map(|e| e.count).unwrap_or(0)
        }

        pub fn view_adapter_at(&self, index: u32) -> Option<ViewAdapterRecord> {
            let ext = self.extension(EXTENSION_VIEW)?;
            if index >= ext.count {
                return None;
            }
            let d = self.desc();
            let base = ext.byte_offset as usize + index as usize * 24;
            if base + 24 > d.len() {
                return None;
            }
            Some(ViewAdapterRecord {
                source_index: u32_at(d, base),
                target_index: u32_at(d, base + 4),
                data_accessor: u32_at(d, base + 8),
                size_accessor: u32_at(d, base + 12),
                semantics: u32_at(d, base + 16),
            })
        }

        pub fn range_adapter_count(&self) -> u32 {
            self.extension(EXTENSION_RANGE).map(|e| e.count).unwrap_or(0)
        }

        pub fn range_adapter_at(&self, index: u32) -> Option<RangeAdapterRecord> {
            let ext = self.extension(EXTENSION_RANGE)?;
            if index >= ext.count {
                return None;
            }
            let d = self.desc();
            let base = ext.byte_offset as usize + index as usize * 16;
            if base + 16 > d.len() {
                return None;
            }
            Some(RangeAdapterRecord {
                source_index: u32_at(d, base),
                size_accessor: u32_at(d, base + 4),
                at_accessor: u32_at(d, base + 8),
                semantics: u32_at(d, base + 12),
            })
        }

        pub fn interface_count(&self) -> u32 {
            self.extension(EXTENSION_INTERFACE).map(|e| e.count).unwrap_or(0)
        }

        pub fn interface_at(&self, index: u32) -> Option<InterfaceRecord> {
            let ext = self.extension(EXTENSION_INTERFACE)?;
            if index >= ext.count {
                return None;
            }
            let d = self.desc();
            let base = ext.byte_offset as usize + index as usize * INTERFACE_RECORD_SIZE;
            if base + INTERFACE_RECORD_SIZE > d.len() {
                return None;
            }
            Some(InterfaceRecord {
                id: hash_at(d, base),
                version: u32_at(d, base + 16),
                method_begin: u32_at(d, base + 20),
                method_count: u32_at(d, base + 24),
                flags: u32_at(d, base + 28),
                name_offset: u32_at(d, base + 32),
            })
        }

        pub fn find_interface(&self, id: Hash128) -> Option<InterfaceRecord> {
            (0..self.interface_count())
                .filter_map(|i| self.interface_at(i))
                .find(|r| r.id == id)
        }

        pub fn interface_method_count(&self) -> u32 {
            self.extension(EXTENSION_INTERFACE_METHOD).map(|e| e.count).unwrap_or(0)
        }

        pub fn interface_method_at(&self, index: u32) -> Option<InterfaceMethodRecord> {
            let ext = self.extension(EXTENSION_INTERFACE_METHOD)?;
            if index >= ext.count {
                return None;
            }
            let d = self.desc();
            let base = ext.byte_offset as usize + index as usize * INTERFACE_METHOD_RECORD_SIZE;
            if base + INTERFACE_METHOD_RECORD_SIZE > d.len() {
                return None;
            }
            Some(InterfaceMethodRecord {
                interface_id: hash_at(d, base),
                signature: hash_at(d, base + 16),
                return_type: hash_at(d, base + 32),
                slot: u32_at(d, base + 48),
                flags: u32_at(d, base + 52),
                parameter_begin: u32_at(d, base + 60),
                parameter_count: u32_at(d, base + 64),
            })
        }

        /// Type-table index of an interface method parameter, or `None`.
        pub fn interface_method_parameter(&self, index: u32) -> Option<u32> {
            let ext = self.extension(EXTENSION_INTERFACE_PARAMETER)?;
            if index >= ext.count {
                return None;
            }
            let d = self.desc();
            let base = ext.byte_offset as usize + index as usize * INTERFACE_PARAMETER_RECORD_SIZE;
            if base + INTERFACE_PARAMETER_RECORD_SIZE > d.len() {
                return None;
            }
            Some(u32_at(d, base))
        }

        pub fn interface_implementation_count(&self) -> u32 {
            self.extension(EXTENSION_IMPLEMENTATION).map(|e| e.count).unwrap_or(0)
        }

        pub fn interface_implementation_at(&self, index: u32) -> Option<InterfaceImplementationRecord> {
            let ext = self.extension(EXTENSION_IMPLEMENTATION)?;
            if index >= ext.count {
                return None;
            }
            let d = self.desc();
            let base = ext.byte_offset as usize + index as usize * INTERFACE_IMPLEMENTATION_RECORD_SIZE;
            if base + INTERFACE_IMPLEMENTATION_RECORD_SIZE > d.len() {
                return None;
            }
            Some(InterfaceImplementationRecord {
                interface_id: hash_at(d, base),
                concrete_type: hash_at(d, base + 16),
                binding_begin: u32_at(d, base + 32),
                binding_count: u32_at(d, base + 36),
                flags: u32_at(d, base + 40),
            })
        }

        pub fn method_binding_count(&self) -> u32 {
            self.extension(EXTENSION_METHOD_BINDING).map(|e| e.count).unwrap_or(0)
        }

        pub fn method_binding_at(&self, index: u32) -> Option<MethodBindingRecord> {
            let ext = self.extension(EXTENSION_METHOD_BINDING)?;
            if index >= ext.count {
                return None;
            }
            let d = self.desc();
            let base = ext.byte_offset as usize + index as usize * METHOD_BINDING_RECORD_SIZE;
            if base + METHOD_BINDING_RECORD_SIZE > d.len() {
                return None;
            }
            Some(MethodBindingRecord {
                interface_id: hash_at(d, base),
                method_signature: hash_at(d, base + 16),
                native_function: hash_at(d, base + 32),
                slot: u32_at(d, base + 48),
                flags: u32_at(d, base + 52),
            })
        }

        /// Target platform identity, or `None` when the region has none.
        pub fn target(&self) -> Option<TargetDescription> {
            let ext = self.extension(EXTENSION_TARGET)?;
            if ext.count < 1 {
                return None;
            }
            let d = self.desc();
            let base = ext.byte_offset as usize;
            if base + 32 > d.len() {
                return None;
            }
            Some(TargetDescription {
                arch: u32_at(d, base),
                os: u32_at(d, base + 4),
                target_abi: u32_at(d, base + 8),
                compiler: u32_at(d, base + 12),
                calling_convention: u32_at(d, base + 16),
                pointer_bits: u32_at(d, base + 20),
                endianness: u32_at(d, base + 24),
            })
        }

        /// Verify the region's target against `expected`, returning
        /// TARGET_COMPATIBLE / TARGET_INCOMPATIBLE / TARGET_UNVERIFIABLE.
        pub fn verify_target(&self, expected: TargetExpectation) -> u32 {
            let target = match self.target() {
                Some(target) => target,
                None => return TARGET_UNVERIFIABLE,
            };
            let mut incompatible = false;
            let mut unverifiable = false;
            {
                let mut compare = |want: u32, have: u32| {
                    if want != 0 {
                        if have == 0 {
                            unverifiable = true;
                        } else if have != want {
                            incompatible = true;
                        }
                    }
                };
                compare(expected.arch, target.arch);
                compare(expected.os, target.os);
                compare(expected.target_abi, target.target_abi);
                compare(expected.pointer_bits, target.pointer_bits);
                compare(expected.endianness, target.endianness);
            }
            if target.pointer_bits != 0 {
                let pointer_bytes = target.pointer_bits / 8;
                for i in 0..self.header.type_count {
                    if let Some(record) = self.type_at(i) {
                        if record.kind == 3 && record.size != pointer_bytes {
                            incompatible = true;
                            break;
                        }
                    }
                }
            }
            if incompatible {
                TARGET_INCOMPATIBLE
            } else if unverifiable {
                TARGET_UNVERIFIABLE
            } else {
                TARGET_COMPATIBLE
            }
        }

        fn type_size_by_id(&self, id: Hash128) -> Option<u32> {
            (0..self.header.type_count)
                .filter_map(|i| self.type_at(i))
                .find(|t| t.id == id)
                .map(|t| t.size)
        }

        /// Structured ABI verification: platform / layout / calling convention,
        /// each reported independently. Facts that need the compiled binary are
        /// not claimed here and come back unverifiable.
        pub fn verify_abi(&self, expected: TargetExpectation) -> AbiVerification {
            let mut platform_incompatible = false;
            let mut platform_unverifiable = false;
            let mut pointer_bits = 0u32;
            match self.target() {
                None => platform_unverifiable = true,
                Some(target) => {
                    pointer_bits = target.pointer_bits;
                    let mut compare = |want: u32, have: u32| {
                        if want != 0 {
                            if have == 0 {
                                platform_unverifiable = true;
                            } else if have != want {
                                platform_incompatible = true;
                            }
                        }
                    };
                    compare(expected.arch, target.arch);
                    compare(expected.os, target.os);
                    compare(expected.target_abi, target.target_abi);
                    compare(expected.pointer_bits, target.pointer_bits);
                    compare(expected.endianness, target.endianness);
                }
            }

            let mut layout_incompatible = false;
            let mut layout_unverifiable = false;
            let pointer_bytes = if pointer_bits != 0 { pointer_bits / 8 } else { 0 };
            for i in 0..self.header.type_count {
                if let Some(record) = self.type_at(i) {
                    if record.kind == 3 {
                        if pointer_bytes == 0 {
                            layout_unverifiable = true;
                        } else if record.size != pointer_bytes {
                            layout_incompatible = true;
                            break;
                        }
                    }
                }
            }
            if !layout_incompatible {
                for i in 0..self.header.field_count {
                    if let Some(field) = self.field_at(i) {
                        match (
                            self.type_size_by_id(field.owner_type),
                            self.type_size_by_id(field.type_id),
                        ) {
                            (Some(owner), Some(field_size)) => {
                                if field.offset > owner || field_size > owner - field.offset {
                                    layout_incompatible = true;
                                    break;
                                }
                            }
                            _ => layout_unverifiable = true,
                        }
                    }
                }
            }

            let mut cc_incompatible = false;
            let mut cc_unverifiable = self.header.function_count == 0;
            for i in 0..self.header.function_count {
                if let Some(function) = self.function_at(i) {
                    if function.calling_convention == 0 {
                        cc_unverifiable = true;
                    } else if expected.calling_convention != 0
                        && function.calling_convention != expected.calling_convention
                    {
                        cc_incompatible = true;
                    }
                }
            }

            let platform = if platform_incompatible {
                TARGET_INCOMPATIBLE
            } else if platform_unverifiable {
                TARGET_UNVERIFIABLE
            } else {
                TARGET_COMPATIBLE
            };
            let layout = if layout_incompatible {
                TARGET_INCOMPATIBLE
            } else if layout_unverifiable {
                TARGET_UNVERIFIABLE
            } else {
                TARGET_COMPATIBLE
            };
            let calling_convention = if cc_incompatible {
                TARGET_INCOMPATIBLE
            } else if cc_unverifiable {
                TARGET_UNVERIFIABLE
            } else {
                TARGET_COMPATIBLE
            };
            AbiVerification {
                platform,
                layout,
                calling_convention,
            }
        }

        pub fn name(&self, offset: u32) -> Option<&'a str> {
            if self.header.names_offset == 0 {
                return None;
            }
            let start = (self.header.names_offset + offset as u64) as usize;
            if start >= self.data.len() {
                return None;
            }
            let rest = &self.data[start..];
            let end = rest.iter().position(|&c| c == 0)?;
            core::str::from_utf8(&rest[..end]).ok()
        }

        pub fn find_type_by_name(&self, name: &str) -> Option<TypeRecord> {
            (0..self.header.type_count)
                .filter_map(|i| self.type_at(i))
                .find(|t| self.name(t.name_offset) == Some(name))
        }

        pub fn type_index_by_name(&self, name: &str) -> Option<u32> {
            (0..self.header.type_count).find(|&i| self.type_at(i).and_then(|t| self.name(t.name_offset)) == Some(name))
        }

        pub fn find_type_by_id(&self, id: Hash128) -> Option<TypeRecord> {
            (0..self.header.type_count)
                .filter_map(|i| self.type_at(i))
                .find(|t| t.id == id)
        }

        pub fn find_function_by_name(&self, name: &str) -> Option<FunctionRecord> {
            (0..self.header.function_count)
                .filter_map(|i| self.function_at(i))
                .find(|f| self.name(f.name_offset) == Some(name))
        }

        pub fn function_index_by_name(&self, name: &str) -> Option<u32> {
            (0..self.header.function_count)
                .find(|&i| self.function_at(i).and_then(|f| self.name(f.name_offset)) == Some(name))
        }

        pub fn find_field(&self, owner: Hash128, name: &str) -> Option<FieldRecord> {
            (0..self.header.field_count)
                .filter_map(|i| self.field_at(i))
                .find(|f| f.owner_type == owner && self.name(f.name_offset) == Some(name))
        }
    }
}
