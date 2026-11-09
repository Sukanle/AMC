// amc-rustc-helper — v0.4.0 Rust semantic adapter producer.
//
// The helper links rustc's driver libraries in an isolated process and emits
// the stable `amc.rust.semantic/1` envelope. The first rustc_driver increment
// reports target, crate and provenance facts; type/function extraction follows
// in later increments without changing the envelope.
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
extern crate rustc_target;

use rustc_abi::{Endian, ExternAbi, Integer, IntegerType};
use rustc_driver::{Callbacks, Compilation};
use rustc_hir::def::DefKind;
use rustc_hir::def_id::{DefId, LOCAL_CRATE};
use rustc_hir::{ForeignItemKind, ItemKind, Mutability};
use rustc_interface::interface;
use rustc_middle::middle::codegen_fn_attrs::CodegenFnAttrFlags;
use rustc_middle::ty::{Ty, TyCtxt, TyKind, TypingEnv, Visibility};

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
}

fn usage() -> String {
    "usage: amc-rustc-helper --crate-name <name> --source <path> \
     [--edition <edition>] [--target <triple>] [--output <path>]\n\
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

fn type_spelling(tcx: TyCtxt<'_>, ty: Ty<'_>) -> String {
    match ty.kind() {
        TyKind::Adt(adt, args) => {
            let mut spelling = format!("{}::{}", tcx.crate_name(adt.did().krate), tcx.def_path_str(adt.did()));
            if !args.is_empty() {
                spelling.push_str(&format!("{:?}", args));
            }
            spelling
        }
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
            format!("[{}; {:?}]", type_spelling(tcx, *element), length)
        }
        _ => format!("{:?}", ty),
    }
}

fn type_id(tcx: TyCtxt<'_>, ty: Ty<'_>) -> String {
    format!("rust:{}", type_spelling(tcx, ty))
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

fn emit_type(tcx: TyCtxt<'_>, def_id: DefId) -> Option<String> {
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
    let size = layout.map(|layout| layout.layout.size().bytes()).unwrap_or(0);
    let align = layout.map(|layout| layout.layout.align().abi.bytes()).unwrap_or(1);
    let name = tcx.item_name(def_id).to_string();
    let id = if kind == "alias" {
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

    let mut fields = Vec::new();
    if let TyKind::Adt(adt, _) = ty.kind() {
        if !adt.is_enum() {
            if let Some(layout) = layout {
                for (index, field) in adt.non_enum_variant().fields.iter().enumerate() {
                    let field_ty = tcx.type_of(field.did).instantiate_identity().skip_normalization();
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
    }

    Some(format!(
        "{{\"id\": {}, \"name\": {}, \"kind\": {}, \"repr\": [{}], \"size\": {}, \"align\": {}, \"fields\": [{}]}}",
        json_string(&id),
        json_string(&name),
        json_string(kind),
        repr_json,
        size,
        align,
        fields.join(", ")
    ))
}

fn emit_function(tcx: TyCtxt<'_>, def_id: DefId) -> Option<String> {
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
    Some(format!(
        "{{\"name\": {}, \"symbol\": {}, \"abi\": {}, \"visibility\": {}, \"return_type\": {}, \"parameters\": [{}]}}",
        json_string(&name),
        json_string(&symbol),
        json_string(abi),
        json_string(visibility_text),
        json_string(&type_id(tcx, signature.output().skip_binder())),
        parameters.join(", ")
    ))
}

struct HelperCallbacks {
    output: String,
    source: String,
    rustc: String,
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

        let mut types = Vec::new();
        let mut functions = Vec::new();
        for item_id in tcx.hir_free_items() {
            let item = tcx.hir_item(item_id);
            let def_id = item.owner_id.def_id.to_def_id();
            match item.kind {
                ItemKind::Struct(..) | ItemKind::Union(..) | ItemKind::Enum(..) | ItemKind::TyAlias(..) => {
                    if let Some(value) = emit_type(tcx, def_id) {
                        types.push(value);
                    }
                }
                ItemKind::Fn { .. } => {
                    if let Some(value) = emit_function(tcx, def_id) {
                        functions.push(value);
                    }
                }
                _ => {}
            }
        }
        for foreign_id in tcx.hir_crate_items(()).foreign_items() {
            let item = tcx.hir_foreign_item(foreign_id);
            if let ForeignItemKind::Fn(..) = item.kind {
                let def_id = item.owner_id.def_id.to_def_id();
                if let Some(value) = emit_function(tcx, def_id) {
                    functions.push(value);
                }
            }
        }
        let types_json = types.join(", ");
        let functions_json = functions.join(", ");

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
        let crate_json = format!(
            "{{\n    \"name\": {},\n    \"edition\": {},\n    \"features\": []\n  }}",
            json_string(&crate_name),
            json_string(&edition)
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
            "{{\n  \"schema\": {},\n  \"producer\": {},\n  \"target\": {},\n  \"crate\": {},\n  \"types\": [{}],\n  \"functions\": [{}],\n  \"generics\": [],\n  \"traits\": [],\n  \"errors\": [],\n  \"provenance\": {}\n}}\n",
            json_string(SCHEMA),
            producer,
            target_json,
            crate_json,
            types_json,
            functions_json,
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

    let mut callbacks = HelperCallbacks {
        output: options.output,
        source: options.source,
        rustc: rustc_release(),
    };
    rustc_driver::run_compiler(&args, &mut callbacks);
    let _ = fs::remove_file(&metadata);
}
