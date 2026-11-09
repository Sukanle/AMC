namespace skl::amc::generated {
using TypeId = ::skl::abix::model::TypeId;
using LayoutInfo = ::skl::abix::metadata::LayoutInfo;
using FieldInfo = ::skl::abix::metadata::FieldDescriptor;
using ParameterInfo = ::skl::abix::metadata::ParameterDescriptor;
using FunctionInfo = ::skl::abix::metadata::FunctionDescriptor;
using SymbolInfo = ::skl::abix::metadata::SymbolDescriptor;
using ModuleInfo = ::skl::abix::metadata::ModuleDescriptor;
enum class MapOpcode : uint8_t {
    COPY_FIELD,
    CONVERT_INT,
    CONVERT_FLOAT,
    ADD_DEFAULT,
    SKIP_FIELD,
};
struct MapOperation {
    MapOpcode opcode;
    uint32_t source_field;
    uint32_t target_field;
    uint32_t source_offset;
    uint32_t target_offset;
    uint32_t byte_count;
    TypeId auxiliary;
};
template<typename Source, typename Target>
struct MapPrivate;
template<typename T>
struct TypeTraits;
template<typename T, typename E>
struct Result {
    bool ok = false;
    T value{};
    E error{};
    explicit operator bool() const noexcept { return ok; }
};
