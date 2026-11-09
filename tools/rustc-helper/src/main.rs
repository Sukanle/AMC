// amc-rustc-helper — v0.4.0 Rust semantic adapter producer.
//
// The helper links rustc's driver libraries in an isolated process and emits
// the stable `amc.rust.semantic/1` envelope: target/crate/provenance facts,
// `repr` and generic-instance types, `extern "C"` functions, generics, traits,
// impls and `Result` error enums.
//
// Build (stable toolchain with the rustc-dev component):
//
//   RUSTC_BOOTSTRAP=1 rustc --edition 2021 \
//     -L "$(rustc --print sysroot)/lib" \
//     -L "$(rustc --print sysroot)/lib/rustlib/$(rustc -vV | sed -n 's/^host: //p')/lib" \
//     -C link-arg=-Wl,-rpath,$(rustc --print sysroot)/lib \
//     tools/rustc-helper/src/main.rs -o amc-rustc-helper

#![feature(rustc_private)]

extern crate rustc_abi;
extern crate rustc_driver;
extern crate rustc_hir;
extern crate rustc_interface;
extern crate rustc_middle;
extern crate rustc_session;
extern crate rustc_span;
extern crate rustc_target;

use rustc_abi::{Endian, ExternAbi, Integer, IntegerType};
use rustc_driver::{Callbacks, Compilation};
use rustc_hir::def::{DefKind, Namespace};
use rustc_hir::def_id::{DefId, LOCAL_CRATE};
use rustc_hir::{ForeignItemKind, ItemKind, Mutability};
use rustc_interface::interface;
use rustc_middle::middle::codegen_fn_attrs::CodegenFnAttrFlags;
use rustc_middle::ty::print::{FmtPrinter, Print};
use rustc_middle::ty::{AssocKind, Clause, GenericParamDefKind, Ty, TyCtxt, TyKind, TypingEnv, Visibility};
use rustc_span::sym;

use std::env;
use std::fs;
use std::process::{Command, Stdio};

const SCHEMA: &str = "amc.rust.semantic/1";
const HELPER_VERSION: &str = "0.1.0";

struct Options {
    crate_name: String,
    edition: String,
    source: String,
    output: String,
    target: Option<String>,
    manifest_path: Option<String>,
    features: Vec<String>,
}

fn usage() -> String {
    "usage: amc-rustc-helper --crate-name <name> --source <path> \
     [--edition <edition>] [--target <triple>] [--output <path>] \
     [--manifest-path <Cargo.toml>] [--features <a,b>]\n\
     \n\
     Emits one amc.rust.semantic/1 document on stdout when --output is omitted."
        .to_string()
}

fn parse_options() -> Result<Options, String> {
    let mut crate_name = String::new();
    let mut edition = String::from("2021");
    let mut source = String::new();
    let mut output = String::new();
    let mut target = None;
    let mut manifest_path = None;
    let mut features = Vec::new();
    let args: Vec<String> = env::args().skip(1).collect();
    let mut index = 0;
    while index < args.len() {
        let flag = &args[index];
        if flag == "--version" {
            println!("amc-rustc-helper {}", HELPER_VERSION);
            std::process::exit(0);
        }
        if flag == "--help" || flag == "-h" {
            println!("{}", usage());
            std::process::exit(0);
        }
        if index + 1 >= args.len() {
            return Err(format!("missing value for {}", flag));
        }
        let value = args[index + 1].clone();
        match flag.as_str() {
            "--crate-name" => crate_name = value,
            "--edition" => edition = value,
            "--source" => source = value,
            "--output" => output = value,
            "--target" => target = Some(value),
            "--manifest-path" => manifest_path = Some(value),
            "--features" => {
                features = value
                    .split(',')
                    .map(|feature| feature.trim().to_string())
                    .filter(|feature| !feature.is_empty())
                    .collect()
            }
            _ => return Err(format!("unknown option: {}", flag)),
        }
        index += 2;
    }
    if crate_name.is_empty() {
        return Err("--crate-name is required".to_string());
    }
    if source.is_empty() {
        return Err("--source is required".to_string());
    }
    Ok(Options {
        crate_name,
        edition,
        source,
        output,
        target,
        manifest_path,
        features,
    })
}

fn json_string(value: &str) -> String {
    let mut out = String::with_capacity(value.len() + 2);
    out.push('"');
    for ch in value.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

#[allow(dead_code)]
#[derive(Debug)]
enum JsonValue {
    Null,
    Bool(bool),
    Number(f64),
    String(String),
    Array(Vec<JsonValue>),
    Object(Vec<(String, JsonValue)>),
}

impl JsonValue {
    fn get(&self, key: &str) -> Option<&JsonValue> {
        match self {
            JsonValue::Object(entries) => entries.iter().find(|(name, _)| name == key).map(|(_, value)| value),
            _ => None,
        }
    }

    fn as_str(&self) -> Option<&str> {
        match self {
            JsonValue::String(value) => Some(value),
            _ => None,
        }
    }

    fn as_array(&self) -> Option<&[JsonValue]> {
        match self {
            JsonValue::Array(values) => Some(values),
            _ => None,
        }
    }
}

struct JsonParser<'a> {
    bytes: &'a [u8],
    position: usize,
}

impl<'a> JsonParser<'a> {
    fn new(text: &'a str) -> Self {
        Self {
            bytes: text.as_bytes(),
            position: 0,
        }
    }

    fn parse(mut self) -> Option<JsonValue> {
        self.skip_space();
        let value = self.value()?;
        self.skip_space();
        (self.position == self.bytes.len()).then_some(value)
    }

    fn skip_space(&mut self) {
        while self.position < self.bytes.len() && self.bytes[self.position].is_ascii_whitespace() {
            self.position += 1;
        }
    }

    fn value(&mut self) -> Option<JsonValue> {
        self.skip_space();
        match *self.bytes.get(self.position)? {
            b'n' => {
                self.literal(b"null")?;
                Some(JsonValue::Null)
            }
            b't' => {
                self.literal(b"true")?;
                Some(JsonValue::Bool(true))
            }
            b'f' => {
                self.literal(b"false")?;
                Some(JsonValue::Bool(false))
            }
            b'"' => self.string().map(JsonValue::String),
            b'[' => self.array(),
            b'{' => self.object(),
            _ => self.number(),
        }
    }

    fn literal(&mut self, text: &[u8]) -> Option<()> {
        if self.bytes.get(self.position..self.position + text.len())? == text {
            self.position += text.len();
            Some(())
        } else {
            None
        }
    }

    fn string(&mut self) -> Option<String> {
        self.position += 1;
        let mut out = String::new();
        while self.position < self.bytes.len() {
            match self.bytes[self.position] {
                b'"' => {
                    self.position += 1;
                    return Some(out);
                }
                b'\\' => {
                    self.position += 1;
                    match *self.bytes.get(self.position)? {
                        b'"' => out.push('"'),
                        b'\\' => out.push('\\'),
                        b'/' => out.push('/'),
                        b'b' => out.push('\u{0008}'),
                        b'f' => out.push('\u{000c}'),
                        b'n' => out.push('\n'),
                        b'r' => out.push('\r'),
                        b't' => out.push('\t'),
                        b'u' => {
                            let hex = self.bytes.get(self.position + 1..self.position + 5)?;
                            let code = u32::from_str_radix(std::str::from_utf8(hex).ok()?, 16).ok()?;
                            out.push(char::from_u32(code)?);
                            self.position += 4;
                        }
                        _ => return None,
                    }
                    self.position += 1;
                }
                byte => {
                    out.push(byte as char);
                    self.position += 1;
                }
            }
        }
        None
    }

    fn number(&mut self) -> Option<JsonValue> {
        let start = self.position;
        while self.position < self.bytes.len()
            && matches!(
                self.bytes[self.position],
                b'-' | b'+' | b'.' | b'e' | b'E' | b'0'..=b'9'
            )
        {
            self.position += 1;
        }
        let text = std::str::from_utf8(&self.bytes[start..self.position]).ok()?;
        text.parse::<f64>().ok().map(JsonValue::Number)
    }

    fn array(&mut self) -> Option<JsonValue> {
        self.position += 1;
        let mut values = Vec::new();
        self.skip_space();
        if self.bytes.get(self.position) == Some(&b']') {
            self.position += 1;
            return Some(JsonValue::Array(values));
        }
        loop {
            values.push(self.value()?);
            self.skip_space();
            match *self.bytes.get(self.position)? {
                b',' => self.position += 1,
                b']' => {
                    self.position += 1;
                    return Some(JsonValue::Array(values));
                }
                _ => return None,
            }
        }
    }

    fn object(&mut self) -> Option<JsonValue> {
        self.position += 1;
        let mut entries = Vec::new();
        self.skip_space();
        if self.bytes.get(self.position) == Some(&b'}') {
            self.position += 1;
            return Some(JsonValue::Object(entries));
        }
        loop {
            self.skip_space();
            let key = self.string()?;
            self.skip_space();
            if self.bytes.get(self.position) != Some(&b':') {
                return None;
            }
            self.position += 1;
            let value = self.value()?;
            entries.push((key, value));
            self.skip_space();
            match *self.bytes.get(self.position)? {
                b',' => self.position += 1,
                b'}' => {
                    self.position += 1;
                    return Some(JsonValue::Object(entries));
                }
                _ => return None,
            }
        }
    }
}

struct CargoFacts {
    name: String,
    edition: String,
    features: Vec<String>,
}

fn discover_manifest(source: &str) -> Option<std::path::PathBuf> {
    let mut directory = std::path::Path::new(source).parent()?.to_path_buf();
    loop {
        let candidate = directory.join("Cargo.toml");
        if candidate.is_file() {
            return Some(candidate);
        }
        if !directory.pop() {
            return None;
        }
    }
}

fn cargo_facts(options: &Options) -> Option<CargoFacts> {
    let manifest = options
        .manifest_path
        .as_ref()
        .map(std::path::PathBuf::from)
        .or_else(|| discover_manifest(&options.source))?;
    let output = Command::new("cargo")
        .arg("metadata")
        .arg("--no-deps")
        .arg("--format-version")
        .arg("1")
        .arg("--manifest-path")
        .arg(&manifest)
        .stdin(Stdio::null())
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }
    let document = JsonParser::new(&String::from_utf8_lossy(&output.stdout)).parse()?;
    let packages = document.get("packages")?.as_array()?;
    let target = std::fs::canonicalize(&manifest).ok();
    for package in packages {
        let package_manifest = package
            .get("manifest_path")
            .and_then(JsonValue::as_str)
            .map(std::path::PathBuf::from);
        let matches = match (&target, &package_manifest) {
            (Some(target), Some(path)) => std::fs::canonicalize(path).ok().as_ref() == Some(target),
            _ => true,
        };
        if !matches {
            continue;
        }
        let name = package.get("name").and_then(JsonValue::as_str)?.to_string();
        let edition = package
            .get("edition")
            .and_then(JsonValue::as_str)
            .unwrap_or("2021")
            .to_string();
        let mut features = options.features.clone();
        if let Some(JsonValue::Object(entries)) = package.get("features") {
            for (feature, _) in entries {
                features.push(feature.clone());
            }
        }
        features.sort();
        features.dedup();
        return Some(CargoFacts {
            name,
            edition,
            features,
        });
    }
    None
}

fn rustc_release() -> String {
    Command::new("rustc")
        .arg("--version")
        .stdin(Stdio::null())
        .output()
        .ok()
        .and_then(|output| {
            if output.status.success() {
                Some(String::from_utf8_lossy(&output.stdout).trim().to_string())
            } else {
                None
            }
        })
        .unwrap_or_else(|| "unknown".to_string())
}

fn item_id(tcx: TyCtxt<'_>, def_id: DefId) -> String {
    format!("rust:{}::{}", tcx.crate_name(def_id.krate), tcx.def_path_str(def_id))
}

fn path_string(tcx: TyCtxt<'_>, def_id: DefId) -> String {
    if def_id.krate == LOCAL_CRATE {
        tcx.def_path_str(def_id)
    } else {
        format!("{}::{}", tcx.crate_name(def_id.krate), tcx.def_path_str(def_id))
    }
}

fn type_spelling<'tcx>(tcx: TyCtxt<'tcx>, ty: Ty<'tcx>) -> String {
    match ty.kind() {
        TyKind::Adt(adt, args) => format!(
            "{}::{}",
            tcx.crate_name(adt.did().krate),
            tcx.def_path_str_with_args(adt.did(), args)
        ),
        TyKind::RawPtr(pointee, mutability) => {
            let qualifier = if *mutability == Mutability::Mut {
                "*mut "
            } else {
                "*const "
            };
            format!("{}{}", qualifier, type_spelling(tcx, *pointee))
        }
        TyKind::Ref(_, pointee, mutability) => {
            let qualifier = if *mutability == Mutability::Mut { "&mut " } else { "&" };
            format!("{}{}", qualifier, type_spelling(tcx, *pointee))
        }
        TyKind::Array(element, length) => {
            let count = length
                .try_to_target_usize(tcx)
                .map(|value| value.to_string())
                .unwrap_or_else(|| format!("{:?}", length));
            format!("[{}; {}]", type_spelling(tcx, *element), count)
        }
        _ => format!("{:?}", ty),
    }
}

fn type_id<'tcx>(tcx: TyCtxt<'tcx>, ty: Ty<'tcx>) -> String {
    format!("rust:{}", type_spelling(tcx, ty))
}

fn clause_string<'tcx>(tcx: TyCtxt<'tcx>, clause: Clause<'tcx>) -> String {
    FmtPrinter::print_string(tcx, Namespace::TypeNS, |printer| clause.print(printer))
        .unwrap_or_else(|_| format!("{:?}", clause))
}

fn generic_parameters<'tcx>(tcx: TyCtxt<'tcx>, def_id: DefId) -> (Vec<String>, Vec<String>) {
    let generics = tcx.generics_of(def_id);
    let mut parameters = Vec::new();
    for parameter in &generics.own_params {
        if parameter.kind.is_synthetic() || parameter.name.to_string() == "Self" {
            continue;
        }
        let kind = match parameter.kind {
            GenericParamDefKind::Lifetime => "lifetime",
            GenericParamDefKind::Type { .. } => "type",
            GenericParamDefKind::Const { .. } => "const",
        };
        let default = parameter
            .default_value(tcx)
            .map(|value| json_string(&format!("{:?}", value.skip_binder())))
            .unwrap_or_else(|| "null".to_string());
        parameters.push(format!(
            "{{\"index\": {}, \"name\": {}, \"kind\": {}, \"default\": {}}}",
            parameter.index,
            json_string(&parameter.name.to_string()),
            json_string(kind),
            default
        ));
    }
    let clauses = tcx.explicit_clauses_of(def_id);
    let mut where_clause = Vec::new();
    for (clause, _) in clauses.clauses.iter() {
        let text = clause_string(tcx, *clause);
        if text.starts_with("Self:") {
            continue;
        }
        where_clause.push(json_string(&text));
    }
    (parameters, where_clause)
}

fn integer_repr(int: IntegerType) -> String {
    match int {
        IntegerType::Pointer(true) => "isize".to_string(),
        IntegerType::Pointer(false) => "usize".to_string(),
        IntegerType::Fixed(kind, signed) => {
            let bits = match kind {
                Integer::I8 => 8,
                Integer::I16 => 16,
                Integer::I32 => 32,
                Integer::I64 => 64,
                Integer::I128 => 128,
            };
            format!("{}{}", if signed { "i" } else { "u" }, bits)
        }
    }
}

fn repr_list(tcx: TyCtxt<'_>, def_id: DefId) -> Vec<String> {
    let ty = tcx.type_of(def_id).instantiate_identity().skip_normalization();
    let TyKind::Adt(adt, _) = ty.kind() else {
        return Vec::new();
    };
    let repr = adt.repr();
    let mut out = Vec::new();
    if repr.c() {
        out.push("C".to_string());
    }
    if repr.transparent() {
        out.push("transparent".to_string());
    }
    if let Some(pack) = repr.pack {
        out.push(format!("packed({})", pack.bytes()));
    }
    if let Some(align) = repr.align {
        out.push(format!("align({})", align.bytes()));
    }
    if let Some(int) = repr.int {
        out.push(integer_repr(int));
    }
    out
}

fn emit_type<'tcx>(tcx: TyCtxt<'tcx>, def_id: DefId) -> Option<(String, String)> {
    let kind = match tcx.def_kind(def_id) {
        DefKind::Struct => "record",
        DefKind::Union => "union",
        DefKind::Enum => "enum",
        DefKind::TyAlias => "alias",
        _ => return None,
    };
    let ty = tcx.type_of(def_id).instantiate_identity().skip_normalization();
    let typing_env = TypingEnv::post_analysis(tcx, def_id);
    let layout = tcx.layout_of(typing_env.as_query_input(ty)).ok();
    let generic = tcx
        .generics_of(def_id)
        .own_params
        .iter()
        .any(|parameter| !parameter.kind.is_synthetic());
    let size = layout.map(|layout| layout.layout.size().bytes()).unwrap_or(0);
    let align = layout.map(|layout| layout.layout.align().abi.bytes()).unwrap_or(1);
    let name = path_string(tcx, def_id);
    let id = if kind == "alias" || generic {
        item_id(tcx, def_id)
    } else {
        type_id(tcx, ty)
    };
    let repr = repr_list(tcx, def_id);
    let repr_json = repr
        .iter()
        .map(|value| json_string(value))
        .collect::<Vec<_>>()
        .join(", ");
    let underlying = if kind == "alias" {
        json_string(&type_id(tcx, ty))
    } else {
        "null".to_string()
    };

    let mut fields = Vec::new();
    let mut variants = Vec::new();
    if let TyKind::Adt(adt, args) = ty.kind() {
        if kind == "alias" {
            // The alias record only carries the resolved layout; its single
            // field is emitted by the AMC importer from the underlying type.
        } else if adt.is_enum() {
            for (index, variant) in adt.variants().iter_enumerated() {
                let discriminant = adt.discriminant_for_variant(tcx, index).val.to_string();
                let mut payload = Vec::new();
                for field in variant.fields.iter() {
                    let field_ty = tcx.type_of(field.did).instantiate(tcx, args).skip_normalization();
                    payload.push(json_string(&type_id(tcx, field_ty)));
                }
                variants.push(format!(
                    "{{\"name\": {}, \"discriminant\": {}, \"payload\": [{}]}}",
                    json_string(&variant.name.to_string()),
                    json_string(&discriminant),
                    payload.join(", ")
                ));
            }
        } else if let Some(layout) = layout {
            for (index, field) in adt.non_enum_variant().fields.iter().enumerate() {
                let field_ty = tcx.type_of(field.did).instantiate(tcx, args).skip_normalization();
                let field_name = field.ident(tcx).to_string();
                let offset = layout.layout.fields().offset(index).bytes();
                fields.push(format!(
                    "{{\"name\": {}, \"offset\": {}, \"type\": {}}}",
                    json_string(&field_name),
                    offset,
                    json_string(&type_id(tcx, field_ty))
                ));
            }
        }
    }

    Some((
        id.clone(),
        format!(
            "{{\"id\": {}, \"name\": {}, \"path\": {}, \"kind\": {}, \"repr\": [{}], \"size\": {}, \"align\": {}, \"generic\": {}, \"underlying\": {}, \"fields\": [{}], \"variants\": [{}]}}",
            json_string(&id),
            json_string(&name),
            json_string(&name),
            json_string(kind),
            repr_json,
            size,
            align,
            generic,
            underlying,
            fields.join(", "),
            variants.join(", ")
        ),
    ))
}

fn emit_instance<'tcx>(
    tcx: TyCtxt<'tcx>,
    ty: Ty<'tcx>,
    adt: rustc_middle::ty::AdtDef<'tcx>,
    args: rustc_middle::ty::GenericArgsRef<'tcx>,
) -> Option<(String, String)> {
    let typing_env = TypingEnv::post_analysis(tcx, adt.did());
    let layout = tcx.layout_of(typing_env.as_query_input(ty)).ok()?;
    let kind = if adt.is_union() {
        "union"
    } else if adt.is_enum() {
        "enum"
    } else {
        "record"
    };
    let name = tcx.def_path_str_with_args(adt.did(), args);
    let id = type_id(tcx, ty);
    let repr = repr_list(tcx, adt.did());
    let repr_json = repr
        .iter()
        .map(|value| json_string(value))
        .collect::<Vec<_>>()
        .join(", ");

    let mut fields = Vec::new();
    let mut variants = Vec::new();
    if adt.is_enum() {
        for (index, variant) in adt.variants().iter_enumerated() {
            let discriminant = adt.discriminant_for_variant(tcx, index).val.to_string();
            let mut payload = Vec::new();
            for field in variant.fields.iter() {
                let field_ty = tcx.type_of(field.did).instantiate(tcx, args).skip_normalization();
                payload.push(json_string(&type_id(tcx, field_ty)));
            }
            variants.push(format!(
                "{{\"name\": {}, \"discriminant\": {}, \"payload\": [{}]}}",
                json_string(&variant.name.to_string()),
                json_string(&discriminant),
                payload.join(", ")
            ));
        }
    } else {
        for (index, field) in adt.non_enum_variant().fields.iter().enumerate() {
            let field_ty = tcx.type_of(field.did).instantiate(tcx, args).skip_normalization();
            let field_name = field.ident(tcx).to_string();
            let offset = layout.layout.fields().offset(index).bytes();
            fields.push(format!(
                "{{\"name\": {}, \"offset\": {}, \"type\": {}}}",
                json_string(&field_name),
                offset,
                json_string(&type_id(tcx, field_ty))
            ));
        }
    }

    Some((
        id.clone(),
        format!(
            "{{\"id\": {}, \"name\": {}, \"path\": {}, \"kind\": {}, \"repr\": [{}], \"size\": {}, \"align\": {}, \"generic\": false, \"fields\": [{}], \"variants\": [{}]}}",
            json_string(&id),
            json_string(&name),
            json_string(&name),
            json_string(kind),
            repr_json,
            layout.layout.size().bytes(),
            layout.layout.align().abi.bytes(),
            fields.join(", "),
            variants.join(", ")
        ),
    ))
}

fn collect_type_instances<'tcx>(
    tcx: TyCtxt<'tcx>,
    ty: Ty<'tcx>,
    instances: &mut std::collections::BTreeMap<String, String>,
    by_decl: &mut std::collections::BTreeMap<String, std::collections::BTreeSet<String>>,
    seen: &mut std::collections::BTreeSet<String>,
) {
    match ty.kind() {
        TyKind::Adt(adt, args) => {
            if !args.is_empty() && adt.did().krate == LOCAL_CRATE {
                let id = type_id(tcx, ty);
                if seen.insert(id.clone()) {
                    if let Some((_, value)) = emit_instance(tcx, ty, *adt, args) {
                        by_decl.entry(item_id(tcx, adt.did())).or_default().insert(id.clone());
                        instances.insert(id, value);
                    }
                }
            }
            for field in adt.all_fields() {
                let field_ty = tcx.type_of(field.did).instantiate(tcx, args).skip_normalization();
                collect_type_instances(tcx, field_ty, instances, by_decl, seen);
            }
        }
        TyKind::RawPtr(pointee, _) | TyKind::Ref(_, pointee, _) => {
            collect_type_instances(tcx, *pointee, instances, by_decl, seen);
        }
        TyKind::Array(element, _) => collect_type_instances(tcx, *element, instances, by_decl, seen),
        _ => {}
    }
}

fn emit_function<'tcx>(tcx: TyCtxt<'tcx>, def_id: DefId) -> Option<String> {
    if !matches!(tcx.def_kind(def_id), DefKind::Fn | DefKind::AssocFn) {
        return None;
    }
    let signature = tcx.fn_sig(def_id).skip_binder();
    let abi = match signature.abi() {
        ExternAbi::C { .. } | ExternAbi::System { .. } => "C",
        _ => return None,
    };
    let visibility = tcx.visibility(def_id);
    let attrs = tcx.codegen_fn_attrs(def_id);
    if visibility != Visibility::Public
        && !attrs.flags.contains(CodegenFnAttrFlags::NO_MANGLE)
        && attrs.symbol_name.is_none()
    {
        return None;
    }
    let name = tcx.item_name(def_id).to_string();
    let symbol = attrs
        .symbol_name
        .map(|symbol| symbol.to_string())
        .unwrap_or_else(|| name.clone());
    let mut parameters = Vec::new();
    let inputs = signature.inputs().skip_binder();
    for (index, input) in inputs.iter().enumerate() {
        parameters.push(format!(
            "{{\"name\": {}, \"type\": {}}}",
            json_string(&format!("arg{}", index)),
            json_string(&type_id(tcx, *input))
        ));
    }
    let visibility_text = if visibility == Visibility::Public {
        "public"
    } else {
        "private"
    };
    let associated = tcx.def_kind(def_id) == DefKind::AssocFn;
    let (kind_text, has_receiver) = if associated {
        match tcx.associated_item(def_id).kind {
            AssocKind::Fn { has_self, .. } => {
                if has_self {
                    ("method", true)
                } else {
                    ("associated", false)
                }
            }
            _ => ("free", false),
        }
    } else {
        ("free", false)
    };
    let owner = tcx.impl_of_assoc(def_id).map(|impl_id| {
        let self_ty = tcx.type_of(impl_id).instantiate_identity().skip_normalization();
        type_id(tcx, self_ty)
    });
    let owner_json = owner
        .map(|value| json_string(&value))
        .unwrap_or_else(|| "null".to_string());
    Some(format!(
        "{{\"name\": {}, \"path\": {}, \"symbol\": {}, \"abi\": {}, \"visibility\": {}, \"kind\": {}, \"has_receiver\": {}, \"owner\": {}, \"return_type\": {}, \"parameters\": [{}]}}",
        json_string(&name),
        json_string(&path_string(tcx, def_id)),
        json_string(&symbol),
        json_string(abi),
        json_string(visibility_text),
        json_string(kind_text),
        has_receiver,
        owner_json,
        json_string(&type_id(tcx, signature.output().skip_binder())),
        parameters.join(", ")
    ))
}

fn abi_name(abi: ExternAbi) -> String {
    match abi {
        ExternAbi::C { .. } | ExternAbi::System { .. } => "C".to_string(),
        other => format!("{:?}", other),
    }
}

fn method_json<'tcx>(tcx: TyCtxt<'tcx>, def_id: DefId, required: bool) -> Option<String> {
    if tcx.def_kind(def_id) != DefKind::AssocFn {
        return None;
    }
    let associated = tcx.associated_item(def_id);
    let has_receiver = matches!(associated.kind, AssocKind::Fn { has_self: true, .. });
    let name = tcx.item_name(def_id).to_string();
    let signature = tcx.fn_sig(def_id).skip_binder();
    let mut parameters = Vec::new();
    for (index, input) in signature.inputs().skip_binder().iter().enumerate() {
        parameters.push(format!(
            "{{\"name\": {}, \"type\": {}}}",
            json_string(&format!("arg{}", index)),
            json_string(&type_id(tcx, *input))
        ));
    }
    Some(format!(
        "{{\"name\": {}, \"path\": {}, \"has_receiver\": {}, \"required\": {}, \"abi\": {}, \"return_type\": {}, \"parameters\": [{}]}}",
        json_string(&name),
        json_string(&path_string(tcx, def_id)),
        has_receiver,
        required,
        json_string(&abi_name(signature.abi())),
        json_string(&type_id(tcx, signature.output().skip_binder())),
        parameters.join(", ")
    ))
}

fn generic_kind(tcx: TyCtxt<'_>, def_id: DefId) -> &'static str {
    match tcx.def_kind(def_id) {
        DefKind::Struct | DefKind::Union | DefKind::Enum | DefKind::TyAlias => "type",
        DefKind::Fn | DefKind::AssocFn => "function",
        DefKind::Trait => "trait",
        _ => "item",
    }
}

fn emit_generic<'tcx>(
    tcx: TyCtxt<'tcx>,
    def_id: DefId,
    instances_by_decl: &std::collections::BTreeMap<String, std::collections::BTreeSet<String>>,
) -> Option<String> {
    let (parameters, where_clause) = generic_parameters(tcx, def_id);
    if parameters.is_empty() && where_clause.is_empty() {
        return None;
    }
    let id = item_id(tcx, def_id);
    let instances = instances_by_decl
        .get(&id)
        .map(|values| {
            values
                .iter()
                .map(|value| json_string(value))
                .collect::<Vec<_>>()
                .join(", ")
        })
        .unwrap_or_default();
    Some(format!(
        "{{\"id\": {}, \"name\": {}, \"path\": {}, \"kind\": {}, \"parameters\": [{}], \"where_clause\": [{}], \"instances\": [{}]}}",
        json_string(&id),
        json_string(&path_string(tcx, def_id)),
        json_string(&path_string(tcx, def_id)),
        json_string(generic_kind(tcx, def_id)),
        parameters.join(", "),
        where_clause.join(", "),
        instances
    ))
}

fn emit_trait<'tcx>(tcx: TyCtxt<'tcx>, def_id: DefId) -> Option<String> {
    if tcx.def_kind(def_id) != DefKind::Trait {
        return None;
    }
    let (parameters, where_clause) = generic_parameters(tcx, def_id);
    let supertraits = tcx
        .explicit_super_clauses_of(def_id)
        .skip_binder()
        .iter()
        .map(|(clause, _)| clause_string(tcx, *clause))
        .filter(|clause| !clause.starts_with("Self:"))
        .map(|clause| json_string(&clause))
        .collect::<Vec<_>>()
        .join(", ");
    let mut methods = Vec::new();
    let mut associated_types = Vec::new();
    for item in tcx.associated_items(def_id).in_definition_order() {
        match item.kind {
            AssocKind::Fn { .. } => {
                let required = !item.defaultness(tcx).has_value();
                if let Some(method) = method_json(tcx, item.def_id, required) {
                    methods.push(method);
                }
            }
            AssocKind::Type { data } => {
                let name = match data {
                    rustc_middle::ty::AssocTypeData::Normal(name) => name.to_string(),
                    rustc_middle::ty::AssocTypeData::Rpitit(_) => "<impl Trait>".to_string(),
                };
                let bounds = tcx
                    .explicit_item_bounds(item.def_id)
                    .skip_binder()
                    .iter()
                    .map(|(clause, _)| json_string(&clause_string(tcx, *clause)))
                    .collect::<Vec<_>>()
                    .join(", ");
                associated_types.push(format!(
                    "{{\"name\": {}, \"bounds\": [{}]}}",
                    json_string(&name),
                    bounds
                ));
            }
            AssocKind::Const { name, .. } => {
                associated_types.push(format!(
                    "{{\"name\": {}, \"kind\": \"const\", \"bounds\": []}}",
                    json_string(&name.to_string())
                ));
            }
        }
    }
    Some(format!(
        "{{\"id\": {}, \"name\": {}, \"path\": {}, \"kind\": \"trait\", \"parameters\": [{}], \"where_clause\": [{}], \"supertraits\": [{}], \"associated_types\": [{}], \"methods\": [{}]}}",
        json_string(&item_id(tcx, def_id)),
        json_string(&path_string(tcx, def_id)),
        json_string(&path_string(tcx, def_id)),
        parameters.join(", "),
        where_clause.join(", "),
        supertraits,
        associated_types.join(", "),
        methods.join(", ")
    ))
}

fn emit_impl<'tcx>(tcx: TyCtxt<'tcx>, def_id: DefId) -> Option<String> {
    if !matches!(tcx.def_kind(def_id), DefKind::Impl { .. }) {
        return None;
    }
    let self_ty = tcx.type_of(def_id).instantiate_identity().skip_normalization();
    let (parameters, where_clause) = generic_parameters(tcx, def_id);
    let trait_id = tcx.impl_opt_trait_id(def_id).map(|trait_id| item_id(tcx, trait_id));
    let has_trait = trait_id.is_some();
    let trait_json = trait_id
        .as_ref()
        .map(|value| json_string(value))
        .unwrap_or_else(|| "null".to_string());
    let negative = has_trait && matches!(tcx.impl_polarity(def_id), rustc_middle::ty::ImplPolarity::Negative);
    let mut methods = Vec::new();
    for item in tcx.associated_items(def_id).in_definition_order() {
        if item.is_fn() {
            if let Some(method) = method_json(tcx, item.def_id, false) {
                methods.push(method);
            }
        }
    }
    Some(format!(
        "{{\"id\": {}, \"name\": {}, \"kind\": \"impl\", \"self_type\": {}, \"trait\": {}, \"negative\": {}, \"parameters\": [{}], \"where_clause\": [{}], \"methods\": [{}]}}",
        json_string(&item_id(tcx, def_id)),
        json_string(&path_string(tcx, def_id)),
        json_string(&type_id(tcx, self_ty)),
        trait_json,
        negative,
        parameters.join(", "),
        where_clause.join(", "),
        methods.join(", ")
    ))
}

fn error_type<'tcx>(tcx: TyCtxt<'tcx>, ty: Ty<'tcx>) -> Option<Ty<'tcx>> {
    let TyKind::Adt(adt, args) = ty.kind() else {
        return None;
    };
    if !tcx.is_diagnostic_item(sym::Result, adt.did()) || args.len() < 2 {
        return None;
    }
    Some(args.type_at(1))
}

fn emit_error<'tcx>(tcx: TyCtxt<'tcx>, ty: Ty<'tcx>, functions: &[String]) -> Option<String> {
    let name = match ty.kind() {
        TyKind::Adt(adt, args) => tcx.def_path_str_with_args(adt.did(), args),
        _ => type_spelling(tcx, ty),
    };
    let id = type_id(tcx, ty);
    let (kind, variants) = match ty.kind() {
        TyKind::Adt(adt, args) if adt.is_enum() => {
            let mut variants = Vec::new();
            for (index, variant) in adt.variants().iter_enumerated() {
                let discriminant = adt.discriminant_for_variant(tcx, index).val.to_string();
                let payload = match variant.fields.iter().next() {
                    Some(field) => {
                        let field_ty = field.ty(tcx, args).skip_normalization();
                        json_string(&type_id(tcx, field_ty))
                    }
                    None => "null".to_string(),
                };
                variants.push(format!(
                    "{{\"name\": {}, \"discriminant\": {}, \"payload\": {}}}",
                    json_string(&variant.name.to_string()),
                    json_string(&discriminant),
                    payload
                ));
            }
            ("enum", variants.join(", "))
        }
        TyKind::Adt(..) => ("record", String::new()),
        _ => ("primitive", String::new()),
    };
    let functions = functions
        .iter()
        .map(|function| json_string(function))
        .collect::<Vec<_>>()
        .join(", ");
    Some(format!(
        "{{\"id\": {}, \"name\": {}, \"path\": {}, \"kind\": {}, \"variants\": [{}], \"functions\": [{}]}}",
        json_string(&id),
        json_string(&name),
        json_string(&name),
        json_string(kind),
        variants,
        functions
    ))
}

fn collect_function_semantics<'tcx>(
    tcx: TyCtxt<'tcx>,
    def_id: DefId,
    instances: &mut std::collections::BTreeMap<String, String>,
    by_decl: &mut std::collections::BTreeMap<String, std::collections::BTreeSet<String>>,
    seen: &mut std::collections::BTreeSet<String>,
    errors: &mut std::collections::BTreeMap<String, (Ty<'tcx>, std::collections::BTreeSet<String>)>,
) {
    if !matches!(tcx.def_kind(def_id), DefKind::Fn | DefKind::AssocFn) {
        return;
    }
    let signature = tcx.fn_sig(def_id).skip_binder();
    for input in signature.inputs().skip_binder() {
        collect_type_instances(tcx, *input, instances, by_decl, seen);
    }
    let output = signature.output().skip_binder();
    collect_type_instances(tcx, output, instances, by_decl, seen);
    if let Some(error_ty) = error_type(tcx, output) {
        let id = type_id(tcx, error_ty);
        errors
            .entry(id)
            .or_insert_with(|| (error_ty, std::collections::BTreeSet::new()))
            .1
            .insert(item_id(tcx, def_id));
    }
}

struct HelperCallbacks {
    output: String,
    source: String,
    rustc: String,
    cargo: Option<CargoFacts>,
}

impl Callbacks for HelperCallbacks {
    fn after_analysis(&mut self, _compiler: &interface::Compiler, tcx: TyCtxt<'_>) -> Compilation {
        let target = &tcx.sess.target;
        let triple = target.llvm_target.clone();
        let arch = triple.split('-').next().unwrap_or("unknown").to_string();
        let os = target.options.os.desc().to_string();
        let abi = target.options.env.desc().to_string();
        let pointer_bits = target.pointer_width;
        let endianness = match target.endian {
            Endian::Little => "little",
            Endian::Big => "big",
        };
        let c_long_bits = if os == "windows" { 32 } else { pointer_bits };
        let crate_name = tcx.crate_name(LOCAL_CRATE).to_string();
        let edition = format!("{}", tcx.sess.edition());

        let mut types: std::collections::BTreeMap<String, String> = std::collections::BTreeMap::new();
        let mut functions: std::collections::BTreeMap<String, String> = std::collections::BTreeMap::new();
        let mut traits: std::collections::BTreeMap<String, String> = std::collections::BTreeMap::new();
        let mut impls: std::collections::BTreeMap<String, String> = std::collections::BTreeMap::new();
        let mut generic_items: std::collections::BTreeMap<String, DefId> = std::collections::BTreeMap::new();
        let mut instances: std::collections::BTreeMap<String, String> = std::collections::BTreeMap::new();
        let mut instances_by_decl: std::collections::BTreeMap<String, std::collections::BTreeSet<String>> =
            std::collections::BTreeMap::new();
        let mut seen_instances: std::collections::BTreeSet<String> = std::collections::BTreeSet::new();
        let mut errors: std::collections::BTreeMap<String, (Ty<'_>, std::collections::BTreeSet<String>)> =
            std::collections::BTreeMap::new();

        for free_item_id in tcx.hir_free_items() {
            let item = tcx.hir_item(free_item_id);
            let def_id = item.owner_id.def_id.to_def_id();
            match item.kind {
                ItemKind::Struct(..) | ItemKind::Union(..) | ItemKind::Enum(..) | ItemKind::TyAlias(..) => {
                    generic_items.insert(item_id(tcx, def_id), def_id);
                    if let Some((id, value)) = emit_type(tcx, def_id) {
                        types.insert(id, value);
                    }
                    let ty = tcx.type_of(def_id).instantiate_identity().skip_normalization();
                    collect_type_instances(tcx, ty, &mut instances, &mut instances_by_decl, &mut seen_instances);
                }
                ItemKind::Fn { .. } => {
                    generic_items.insert(item_id(tcx, def_id), def_id);
                    if let Some(value) = emit_function(tcx, def_id) {
                        functions.insert(path_string(tcx, def_id), value);
                    }
                    collect_function_semantics(
                        tcx,
                        def_id,
                        &mut instances,
                        &mut instances_by_decl,
                        &mut seen_instances,
                        &mut errors,
                    );
                }
                ItemKind::Trait { .. } => {
                    generic_items.insert(item_id(tcx, def_id), def_id);
                    if let Some(value) = emit_trait(tcx, def_id) {
                        traits.insert(item_id(tcx, def_id), value);
                    }
                }
                ItemKind::Impl(..) => {
                    generic_items.insert(item_id(tcx, def_id), def_id);
                    if let Some(value) = emit_impl(tcx, def_id) {
                        impls.insert(item_id(tcx, def_id), value);
                    }
                    let self_ty = tcx.type_of(def_id).instantiate_identity().skip_normalization();
                    collect_type_instances(
                        tcx,
                        self_ty,
                        &mut instances,
                        &mut instances_by_decl,
                        &mut seen_instances,
                    );
                }
                _ => {}
            }
        }
        for foreign_id in tcx.hir_crate_items(()).foreign_items() {
            let item = tcx.hir_foreign_item(foreign_id);
            if let ForeignItemKind::Fn(..) = item.kind {
                let def_id = item.owner_id.def_id.to_def_id();
                if let Some(value) = emit_function(tcx, def_id) {
                    functions.insert(path_string(tcx, def_id), value);
                }
                collect_function_semantics(
                    tcx,
                    def_id,
                    &mut instances,
                    &mut instances_by_decl,
                    &mut seen_instances,
                    &mut errors,
                );
            }
        }
        for impl_item_id in tcx.hir_crate_items(()).impl_items() {
            let def_id = impl_item_id.owner_id.def_id.to_def_id();
            if tcx.def_kind(def_id) != DefKind::AssocFn {
                continue;
            }
            generic_items.insert(item_id(tcx, def_id), def_id);
            if let Some(value) = emit_function(tcx, def_id) {
                functions.insert(path_string(tcx, def_id), value);
            }
            collect_function_semantics(
                tcx,
                def_id,
                &mut instances,
                &mut instances_by_decl,
                &mut seen_instances,
                &mut errors,
            );
        }
        for (id, value) in instances {
            types.entry(id).or_insert(value);
        }

        let mut generics: std::collections::BTreeMap<String, String> = std::collections::BTreeMap::new();
        for (id, def_id) in generic_items {
            if let Some(value) = emit_generic(tcx, def_id, &instances_by_decl) {
                generics.insert(id, value);
            }
        }

        let mut error_documents: std::collections::BTreeMap<String, String> = std::collections::BTreeMap::new();
        for (id, (ty, names)) in errors {
            let names: Vec<String> = names.into_iter().collect();
            if let Some(value) = emit_error(tcx, ty, &names) {
                error_documents.insert(id, value);
            }
        }

        let types_json = types.into_values().collect::<Vec<_>>().join(", ");
        let functions_json = functions.into_values().collect::<Vec<_>>().join(", ");
        let generics_json = generics.into_values().collect::<Vec<_>>().join(", ");
        let traits_json = traits
            .into_values()
            .chain(impls.into_values())
            .collect::<Vec<_>>()
            .join(", ");
        let errors_json = error_documents.into_values().collect::<Vec<_>>().join(", ");

        let target_json = format!(
            "{{\n    \"triple\": {},\n    \"arch\": {},\n    \"os\": {},\n    \"abi\": {},\n    \"pointer_bits\": {},\n    \"endianness\": {},\n    \"c_long_bits\": {}\n  }}",
            json_string(&triple),
            json_string(&arch),
            json_string(&os),
            json_string(&abi),
            pointer_bits,
            json_string(endianness),
            c_long_bits
        );
        let cargo_name = self
            .cargo
            .as_ref()
            .map(|cargo| cargo.name.clone())
            .unwrap_or_else(|| crate_name.clone());
        let cargo_edition = self
            .cargo
            .as_ref()
            .map(|cargo| cargo.edition.clone())
            .unwrap_or_else(|| edition.clone());
        let features_json = self
            .cargo
            .as_ref()
            .map(|cargo| {
                cargo
                    .features
                    .iter()
                    .map(|feature| json_string(feature))
                    .collect::<Vec<_>>()
                    .join(", ")
            })
            .unwrap_or_default();
        let crate_json = format!(
            "{{\n    \"name\": {},\n    \"edition\": {},\n    \"features\": [{}]\n  }}",
            json_string(&cargo_name),
            json_string(&cargo_edition),
            features_json
        );
        let producer = format!(
            "{{\n    \"kind\": \"rustc-helper\",\n    \"version\": {},\n    \"rustc\": {}\n  }}",
            json_string(HELPER_VERSION),
            json_string(&self.rustc)
        );
        let provenance = format!(
            "{{\n    \"source\": {},\n    \"rustc\": {}\n  }}",
            json_string(&self.source),
            json_string(&self.rustc)
        );
        let document = format!(
            "{{\n  \"schema\": {},\n  \"producer\": {},\n  \"target\": {},\n  \"crate\": {},\n  \"types\": [{}],\n  \"functions\": [{}],\n  \"generics\": [{}],\n  \"traits\": [{}],\n  \"errors\": [{}],\n  \"provenance\": {}\n}}\n",
            json_string(SCHEMA),
            producer,
            target_json,
            crate_json,
            types_json,
            functions_json,
            generics_json,
            traits_json,
            errors_json,
            provenance
        );

        if self.output.is_empty() || self.output == "-" {
            print!("{}", document);
        } else if let Err(error) = fs::write(&self.output, document) {
            eprintln!("amc-rustc-helper: cannot write {}: {}", self.output, error);
        }
        Compilation::Stop
    }
}

fn main() {
    let options = match parse_options() {
        Ok(options) => options,
        Err(error) => {
            eprintln!("amc-rustc-helper: {}", error);
            eprintln!("{}", usage());
            std::process::exit(2);
        }
    };

    // rustc_private is an unstable interface even on a stable toolchain. The
    // helper is pinned to a rustc version and runs as an isolated process.
    unsafe {
        env::set_var("RUSTC_BOOTSTRAP", "1");
    }

    let metadata = env::temp_dir().join(format!("amc-rustc-helper-{}.rmeta", std::process::id()));
    let metadata_path = metadata.to_string_lossy().into_owned();
    let mut args = vec![
        "rustc".to_string(),
        "--crate-name".to_string(),
        options.crate_name.clone(),
        "--edition".to_string(),
        options.edition.clone(),
        "--crate-type=lib".to_string(),
        "--emit=metadata".to_string(),
        "-o".to_string(),
        metadata_path.clone(),
    ];
    if let Some(target) = &options.target {
        args.push("--target".to_string());
        args.push(target.clone());
    }
    args.push(options.source.clone());

    let cargo = cargo_facts(&options);
    let mut callbacks = HelperCallbacks {
        output: options.output,
        source: options.source,
        rustc: rustc_release(),
        cargo,
    };
    rustc_driver::run_compiler(&args, &mut callbacks);
    let _ = fs::remove_file(&metadata);
}
