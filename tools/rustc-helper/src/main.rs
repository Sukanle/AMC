// amc-rustc-helper — v0.4.0 Rust semantic adapter producer.
//
// The helper links rustc's driver libraries in an isolated process and emits
// the stable `amc.rust.semantic/1` envelope: target/crate/provenance facts,
// `repr` and generic-instance types, `extern "C"` functions, generics, traits,
// impls and `Result` error enums.
//
// Build (stable toolchain with the rustc-dev component):
//
//   RUSTC_BOOTSTRAP=1 cargo build --release --manifest-path tools/rustc-helper/Cargo.toml

#![feature(rustc_private)]

extern crate rustc_abi;
extern crate rustc_driver;
extern crate rustc_hir;
extern crate rustc_interface;
extern crate rustc_middle;
extern crate rustc_session;
extern crate rustc_span;
extern crate rustc_target;

use cargo_metadata::MetadataCommand;
use clap::Parser;
use rustc_abi::{Endian, ExternAbi, Integer, IntegerType};
use rustc_driver::{Callbacks, Compilation};
use rustc_hir::def::{DefKind, Namespace};
use rustc_hir::def_id::{DefId, LOCAL_CRATE};
use rustc_hir::{ForeignItemKind, ItemKind, Mutability};
use rustc_interface::interface;
use rustc_middle::middle::codegen_fn_attrs::CodegenFnAttrFlags;
use rustc_middle::ty::print::{FmtPrinter, Print};
use rustc_middle::ty::{AssocKind, Clause, GenericArg, GenericParamDefKind, Ty, TyCtxt, TyKind, TypingEnv, Visibility};
use rustc_span::sym;
use serde_json::{json, Value};

use std::collections::{BTreeMap, BTreeSet};
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

const SCHEMA: &str = "amc.rust.semantic/1";
const HELPER_VERSION: &str = "0.1.0";

#[derive(Debug, Parser)]
#[command(name = "amc-rustc-helper", version = HELPER_VERSION, about = "Emit amc.rust.semantic/1 for AMC")]
struct Cli {
    /// Rust crate name passed to rustc.
    #[arg(long)]
    crate_name: String,
    /// Rust edition passed to rustc; defaults to the Cargo package edition.
    #[arg(long)]
    edition: Option<String>,
    /// Root Rust source file.
    #[arg(long)]
    source: PathBuf,
    /// Write the document here instead of stdout.
    #[arg(long)]
    output: Option<PathBuf>,
    /// Cross-compilation target triple.
    #[arg(long)]
    target: Option<String>,
    /// Cargo manifest; discovered above `--source` when omitted.
    #[arg(long)]
    manifest_path: Option<PathBuf>,
    /// Additional Cargo features (comma separated, additive).
    #[arg(long, value_delimiter = ',')]
    features: Vec<String>,
    /// Explicit generic instance, repeatable: Template=arg1,arg2
    #[arg(long = "instantiate", action = clap::ArgAction::Append)]
    instantiate: Vec<String>,
}

struct CargoDependency {
    name: String,
    requirement: String,
    kind: String,
    optional: bool,
    path: Option<String>,
}

struct CargoFacts {
    name: String,
    edition: String,
    features: Vec<String>,
    package_id: String,
    manifest_path: String,
    workspace_root: String,
    target_directory: String,
    workspace_members: Vec<String>,
    default_members: Vec<String>,
    dependencies: Vec<CargoDependency>,
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

fn discover_manifest(source: &Path) -> Option<PathBuf> {
    let mut directory = source.parent()?.to_path_buf();
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

fn cargo_facts(cli: &Cli) -> Option<CargoFacts> {
    let manifest = cli.manifest_path.clone().or_else(|| discover_manifest(&cli.source))?;
    let mut command = MetadataCommand::new();
    command.no_deps();
    command.manifest_path(&manifest);
    let metadata = command.exec().ok()?;
    let target = fs::canonicalize(&manifest).ok()?;
    for package in &metadata.packages {
        let package_manifest = fs::canonicalize(package.manifest_path.as_std_path()).ok();
        if package_manifest.as_ref() != Some(&target) {
            continue;
        }
        let mut features = cli.features.clone();
        features.extend(package.features.keys().cloned());
        features.sort();
        features.dedup();
        let dependencies = package
            .dependencies
            .iter()
            .map(|dependency| CargoDependency {
                name: dependency.name.clone(),
                requirement: dependency.req.to_string(),
                kind: dependency.kind.to_string(),
                optional: dependency.optional,
                path: dependency.path.as_ref().map(ToString::to_string),
            })
            .collect();
        let default_members = if metadata.workspace_default_members.is_available() {
            metadata
                .workspace_default_members
                .iter()
                .map(ToString::to_string)
                .collect()
        } else {
            Vec::new()
        };
        return Some(CargoFacts {
            name: package.name.to_string(),
            edition: package.edition.to_string(),
            features,
            package_id: package.id.to_string(),
            manifest_path: package.manifest_path.to_string(),
            workspace_root: metadata.workspace_root.to_string(),
            target_directory: metadata.target_directory.to_string(),
            workspace_members: metadata.workspace_members.iter().map(ToString::to_string).collect(),
            default_members,
            dependencies,
        });
    }
    None
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

fn printed_type<'tcx>(tcx: TyCtxt<'tcx>, ty: Ty<'tcx>) -> String {
    FmtPrinter::print_string(tcx, Namespace::TypeNS, |printer| ty.print(printer))
        .unwrap_or_else(|_| format!("{:?}", ty))
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
        _ => printed_type(tcx, ty),
    }
}

fn type_id<'tcx>(tcx: TyCtxt<'tcx>, ty: Ty<'tcx>) -> String {
    format!("rust:{}", type_spelling(tcx, ty))
}

fn clause_string<'tcx>(tcx: TyCtxt<'tcx>, clause: Clause<'tcx>) -> String {
    FmtPrinter::print_string(tcx, Namespace::TypeNS, |printer| clause.print(printer))
        .unwrap_or_else(|_| format!("{:?}", clause))
}

fn generic_parameters<'tcx>(tcx: TyCtxt<'tcx>, def_id: DefId) -> (Vec<Value>, Vec<String>) {
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
            .map(|value| json!(format!("{:?}", value.skip_binder())))
            .unwrap_or(Value::Null);
        parameters.push(json!({
            "index": parameter.index,
            "name": parameter.name.to_string(),
            "kind": kind,
            "default": default,
        }));
    }
    let clauses = tcx.explicit_clauses_of(def_id);
    let mut where_clause = Vec::new();
    for (clause, _) in clauses.clauses.iter() {
        let text = clause_string(tcx, *clause);
        if text.starts_with("Self:") {
            continue;
        }
        where_clause.push(text);
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

fn enum_variants<'tcx>(tcx: TyCtxt<'tcx>, ty: Ty<'tcx>, adt: &rustc_middle::ty::AdtDef<'tcx>) -> Vec<Value> {
    let mut variants = Vec::new();
    for (index, variant) in adt.variants().iter_enumerated() {
        let discriminant = adt.discriminant_for_variant(tcx, index).val.to_string();
        let payload = variant
            .fields
            .iter()
            .next()
            .map(|field| {
                let field_ty = field.ty(
                    tcx,
                    match ty.kind() {
                        TyKind::Adt(_, args) => *args,
                        _ => unreachable!(),
                    },
                );
                json!(type_id(tcx, field_ty.skip_normalization()))
            })
            .unwrap_or(Value::Null);
        variants.push(json!({
            "name": variant.name.to_string(),
            "discriminant": discriminant,
            "payload": payload,
        }));
    }
    variants
}

fn emit_type<'tcx>(tcx: TyCtxt<'tcx>, def_id: DefId) -> Option<(String, Value)> {
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
    let underlying = if kind == "alias" {
        json!(type_id(tcx, ty))
    } else {
        Value::Null
    };

    let mut fields = Vec::new();
    let mut variants = Vec::new();
    if let TyKind::Adt(adt, args) = ty.kind() {
        if kind == "alias" {
            // The alias record only carries the resolved layout; its single
            // field is emitted by the AMC importer from `underlying`.
        } else if adt.is_enum() {
            variants = enum_variants(tcx, ty, adt);
        } else if let Some(layout) = layout {
            for (index, field) in adt.non_enum_variant().fields.iter().enumerate() {
                let field_ty = tcx.type_of(field.did).instantiate(tcx, args).skip_normalization();
                let field_name = field.ident(tcx).to_string();
                let offset = layout.layout.fields().offset(index).bytes();
                fields.push(json!({
                    "name": field_name,
                    "offset": offset,
                    "type": type_id(tcx, field_ty),
                }));
            }
        }
    }

    Some((
        id.clone(),
        json!({
            "id": id,
            "name": name,
            "path": name,
            "kind": kind,
            "repr": repr,
            "size": size,
            "align": align,
            "generic": generic,
            "underlying": underlying,
            "fields": fields,
            "variants": variants,
        }),
    ))
}

fn emit_instance<'tcx>(
    tcx: TyCtxt<'tcx>,
    ty: Ty<'tcx>,
    adt: rustc_middle::ty::AdtDef<'tcx>,
    args: rustc_middle::ty::GenericArgsRef<'tcx>,
) -> Option<(String, Value)> {
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

    let mut fields = Vec::new();
    let mut variants = Vec::new();
    if adt.is_enum() {
        variants = enum_variants(tcx, ty, &adt);
    } else {
        for (index, field) in adt.non_enum_variant().fields.iter().enumerate() {
            let field_ty = tcx.type_of(field.did).instantiate(tcx, args).skip_normalization();
            let field_name = field.ident(tcx).to_string();
            let offset = layout.layout.fields().offset(index).bytes();
            fields.push(json!({
                "name": field_name,
                "offset": offset,
                "type": type_id(tcx, field_ty),
            }));
        }
    }

    Some((
        id.clone(),
        json!({
            "id": id,
            "name": name,
            "path": name,
            "kind": kind,
            "repr": repr,
            "size": layout.layout.size().bytes(),
            "align": layout.layout.align().abi.bytes(),
            "generic": false,
            "underlying": Value::Null,
            "fields": fields,
            "variants": variants,
        }),
    ))
}

fn collect_type_instances<'tcx>(
    tcx: TyCtxt<'tcx>,
    ty: Ty<'tcx>,
    instances: &mut BTreeMap<String, Value>,
    by_decl: &mut BTreeMap<String, BTreeSet<String>>,
    seen: &mut BTreeSet<String>,
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

fn abi_name(abi: ExternAbi) -> String {
    match abi {
        ExternAbi::C { .. } | ExternAbi::System { .. } => "C".to_string(),
        other => format!("{:?}", other),
    }
}

fn emit_function<'tcx>(tcx: TyCtxt<'tcx>, def_id: DefId) -> Option<Value> {
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
    let parameters: Vec<Value> = signature
        .inputs()
        .skip_binder()
        .iter()
        .enumerate()
        .map(|(index, input)| {
            json!({
                "name": format!("arg{}", index),
                "type": type_id(tcx, *input),
            })
        })
        .collect();
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
    Some(json!({
        "name": name,
        "path": path_string(tcx, def_id),
        "symbol": symbol,
        "abi": abi,
        "visibility": visibility_text,
        "kind": kind_text,
        "has_receiver": has_receiver,
        "async": tcx.asyncness(def_id).is_async(),
        "owner": owner,
        "return_type": type_id(tcx, signature.output().skip_binder()),
        "parameters": parameters,
    }))
}

fn method_json<'tcx>(tcx: TyCtxt<'tcx>, def_id: DefId, required: bool) -> Option<Value> {
    if tcx.def_kind(def_id) != DefKind::AssocFn {
        return None;
    }
    let associated = tcx.associated_item(def_id);
    let has_receiver = matches!(associated.kind, AssocKind::Fn { has_self: true, .. });
    let name = tcx.item_name(def_id).to_string();
    let signature = tcx.fn_sig(def_id).skip_binder();
    let parameters: Vec<Value> = signature
        .inputs()
        .skip_binder()
        .iter()
        .enumerate()
        .map(|(index, input)| {
            json!({
                "name": format!("arg{}", index),
                "type": type_id(tcx, *input),
            })
        })
        .collect();
    Some(json!({
        "name": name,
        "path": path_string(tcx, def_id),
        "has_receiver": has_receiver,
        "required": required,
        "abi": abi_name(signature.abi()),
        "async": tcx.asyncness(def_id).is_async(),
        "return_type": type_id(tcx, signature.output().skip_binder()),
        "parameters": parameters,
    }))
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
    instances_by_decl: &BTreeMap<String, BTreeSet<String>>,
) -> Option<Value> {
    let (parameters, where_clause) = generic_parameters(tcx, def_id);
    if parameters.is_empty() && where_clause.is_empty() {
        return None;
    }
    let id = item_id(tcx, def_id);
    let instances = instances_by_decl
        .get(&id)
        .map(|values| values.iter().cloned().collect::<Vec<_>>())
        .unwrap_or_default();
    Some(json!({
        "id": id,
        "name": path_string(tcx, def_id),
        "path": path_string(tcx, def_id),
        "kind": generic_kind(tcx, def_id),
        "parameters": parameters,
        "where_clause": where_clause,
        "instances": instances,
    }))
}

fn emit_trait<'tcx>(tcx: TyCtxt<'tcx>, def_id: DefId) -> Option<Value> {
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
        .collect::<Vec<_>>();
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
                    .map(|(clause, _)| clause_string(tcx, *clause))
                    .collect::<Vec<_>>();
                associated_types.push(json!({
                    "name": name,
                    "kind": "type",
                    "bounds": bounds,
                }));
            }
            AssocKind::Const { name, .. } => {
                associated_types.push(json!({
                    "name": name.to_string(),
                    "kind": "const",
                    "bounds": [],
                }));
            }
        }
    }
    Some(json!({
        "id": item_id(tcx, def_id),
        "name": path_string(tcx, def_id),
        "path": path_string(tcx, def_id),
        "kind": "trait",
        "parameters": parameters,
        "where_clause": where_clause,
        "supertraits": supertraits,
        "associated_types": associated_types,
        "methods": methods,
    }))
}

fn emit_impl<'tcx>(tcx: TyCtxt<'tcx>, def_id: DefId) -> Option<Value> {
    if !matches!(tcx.def_kind(def_id), DefKind::Impl { .. }) {
        return None;
    }
    let self_ty = tcx.type_of(def_id).instantiate_identity().skip_normalization();
    let (parameters, where_clause) = generic_parameters(tcx, def_id);
    let trait_id = tcx.impl_opt_trait_id(def_id).map(|trait_id| item_id(tcx, trait_id));
    let has_trait = trait_id.is_some();
    let negative = has_trait && matches!(tcx.impl_polarity(def_id), rustc_middle::ty::ImplPolarity::Negative);
    let mut methods = Vec::new();
    let mut associated_types = Vec::new();
    for item in tcx.associated_items(def_id).in_definition_order() {
        match item.kind {
            AssocKind::Fn { .. } => {
                if let Some(method) = method_json(tcx, item.def_id, false) {
                    methods.push(method);
                }
            }
            AssocKind::Type { data } => {
                let name = match data {
                    rustc_middle::ty::AssocTypeData::Normal(name) => name.to_string(),
                    rustc_middle::ty::AssocTypeData::Rpitit(_) => "<impl Trait>".to_string(),
                };
                let value = tcx.type_of(item.def_id).instantiate_identity().skip_normalization();
                associated_types.push(json!({
                    "name": name,
                    "kind": "type",
                    "value": type_id(tcx, value),
                }));
            }
            AssocKind::Const { name, .. } => {
                associated_types.push(json!({
                    "name": name.to_string(),
                    "kind": "const",
                    "value": Value::Null,
                }));
            }
            _ => {}
        }
    }
    Some(json!({
        "id": item_id(tcx, def_id),
        "name": path_string(tcx, def_id),
        "kind": "impl",
        "self_type": type_id(tcx, self_ty),
        "trait": trait_id,
        "negative": negative,
        "parameters": parameters,
        "where_clause": where_clause,
        "associated_types": associated_types,
        "methods": methods,
    }))
}

fn error_type<'tcx>(tcx: TyCtxt<'tcx>, ty: Ty<'tcx>) -> Option<Ty<'tcx>> {
    if let TyKind::Adt(adt, args) = ty.kind() {
        if tcx.is_diagnostic_item(sym::Result, adt.did()) && args.len() >= 2 {
            return Some(args.type_at(1));
        }
    }
    // A local `#[repr(...)]` enum used as an extern return value is a status
    // enum in practice. Record it as a candidate error entity; AMC still
    // decides whether an explicit `[[error]]` contract maps it.
    let TyKind::Adt(adt, _) = ty.kind() else {
        return None;
    };
    if adt.did().krate == LOCAL_CRATE
        && adt.is_enum()
        && (adt.repr().c() || adt.repr().int.is_some() || adt.repr().transparent())
    {
        return Some(ty);
    }
    None
}

fn emit_error<'tcx>(tcx: TyCtxt<'tcx>, ty: Ty<'tcx>, functions: &[String]) -> Option<Value> {
    let name = match ty.kind() {
        TyKind::Adt(adt, args) => tcx.def_path_str_with_args(adt.did(), args),
        _ => type_spelling(tcx, ty),
    };
    let id = type_id(tcx, ty);
    let (kind, variants) = match ty.kind() {
        TyKind::Adt(adt, args) if adt.is_enum() => {
            let variants = adt
                .variants()
                .iter_enumerated()
                .map(|(index, variant)| {
                    let discriminant = adt.discriminant_for_variant(tcx, index).val.to_string();
                    let payload = match variant.fields.iter().next() {
                        Some(field) => {
                            let field_ty = field.ty(tcx, args).skip_normalization();
                            json!(type_id(tcx, field_ty))
                        }
                        None => Value::Null,
                    };
                    json!({
                        "name": variant.name.to_string(),
                        "discriminant": discriminant,
                        "payload": payload,
                    })
                })
                .collect::<Vec<_>>();
            ("enum", variants)
        }
        TyKind::Adt(..) => ("record", Vec::new()),
        _ => ("primitive", Vec::new()),
    };
    Some(json!({
        "id": id,
        "name": name,
        "path": name,
        "kind": kind,
        "variants": variants,
        "functions": functions,
    }))
}

fn collect_function_semantics<'tcx>(
    tcx: TyCtxt<'tcx>,
    def_id: DefId,
    instances: &mut BTreeMap<String, Value>,
    by_decl: &mut BTreeMap<String, BTreeSet<String>>,
    seen: &mut BTreeSet<String>,
    errors: &mut BTreeMap<String, (Ty<'tcx>, BTreeSet<String>)>,
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
            .or_insert_with(|| (error_ty, BTreeSet::new()))
            .1
            .insert(item_id(tcx, def_id));
    }
}

fn collect_lifetimes<'tcx>(tcx: TyCtxt<'tcx>, def_id: DefId, out: &mut Vec<Value>) {
    let owner = item_id(tcx, def_id);
    for parameter in &tcx.generics_of(def_id).own_params {
        if matches!(parameter.kind, GenericParamDefKind::Lifetime) {
            out.push(json!({
                "owner": owner,
                "name": parameter.name.to_string(),
                "kind": "param",
                "bounds": [],
            }));
        }
    }
    for (clause, _) in tcx.explicit_clauses_of(def_id).clauses.iter() {
        let text = clause_string(tcx, *clause);
        if text.contains('\'') {
            out.push(json!({
                "owner": owner,
                "name": "",
                "kind": "outlives",
                "bounds": [text],
            }));
        }
    }
    if matches!(tcx.def_kind(def_id), DefKind::Fn | DefKind::AssocFn) {
        let late_bound = tcx.fn_sig(def_id).skip_binder().bound_vars().len();
        if late_bound > 0 {
            out.push(json!({
                "owner": owner,
                "name": "<late_bound>",
                "kind": "late_bound",
                "bounds": [late_bound.to_string()],
            }));
        }
    }
}

fn collect_function_metadata<'tcx>(
    tcx: TyCtxt<'tcx>,
    def_id: DefId,
    lifetimes: &mut Vec<Value>,
    trait_objects: &mut Vec<Value>,
    seen: &mut BTreeSet<String>,
    async_functions: &mut Vec<Value>,
    containers: &mut Vec<Value>,
    seen_containers: &mut BTreeSet<String>,
) {
    collect_lifetimes(tcx, def_id, lifetimes);
    let owner = item_id(tcx, def_id);
    let signature = tcx.fn_sig(def_id).skip_binder();
    for input in signature.inputs().skip_binder() {
        collect_trait_objects(tcx, &owner, "parameter", *input, trait_objects, seen);
        collect_standard_containers(tcx, &owner, "parameter", *input, containers, seen_containers);
    }
    collect_trait_objects(
        tcx,
        &owner,
        "return",
        signature.output().skip_binder(),
        trait_objects,
        seen,
    );
    collect_standard_containers(
        tcx,
        &owner,
        "return",
        signature.output().skip_binder(),
        containers,
        seen_containers,
    );
    if tcx.asyncness(def_id).is_async() {
        async_functions.push(json!({
            "id": owner,
            "path": path_string(tcx, def_id),
            "name": tcx.item_name(def_id).to_string(),
            "return_type": type_id(tcx, signature.output().skip_binder()),
        }));
    }
}

fn collect_trait_objects<'tcx>(
    tcx: TyCtxt<'tcx>,
    owner: &str,
    context: &str,
    ty: Ty<'tcx>,
    out: &mut Vec<Value>,
    seen: &mut BTreeSet<String>,
) {
    match ty.kind() {
        TyKind::Dynamic(bounds, _) => {
            let key = format!("{}|{}|{}", owner, context, type_spelling(tcx, ty));
            if seen.insert(key) {
                let traits = bounds
                    .iter()
                    .map(|bound| format!("{:?}", bound.skip_binder()))
                    .collect::<Vec<_>>();
                out.push(json!({
                    "owner": owner,
                    "context": context,
                    "type": type_spelling(tcx, ty),
                    "traits": traits,
                }));
            }
        }
        TyKind::Adt(adt, args) => {
            for field in adt.all_fields() {
                let field_ty = tcx.type_of(field.did).instantiate(tcx, args).skip_normalization();
                collect_trait_objects(tcx, owner, context, field_ty, out, seen);
            }
        }
        TyKind::RawPtr(pointee, _) | TyKind::Ref(_, pointee, _) | TyKind::Slice(pointee) => {
            collect_trait_objects(tcx, owner, context, *pointee, out, seen);
        }
        TyKind::Array(element, _) => collect_trait_objects(tcx, owner, context, *element, out, seen),
        TyKind::Tuple(elements) => {
            for element in elements.iter() {
                collect_trait_objects(tcx, owner, context, element, out, seen);
            }
        }
        _ => {}
    }
}

fn standard_container<'tcx>(tcx: TyCtxt<'tcx>, ty: Ty<'tcx>) -> Option<(&'static str, String)> {
    let TyKind::Adt(adt, args) = ty.kind() else {
        return None;
    };
    let def_id = adt.did();
    let path = tcx.def_path_str(def_id);
    if !(path.starts_with("core::") || path.starts_with("alloc::") || path.starts_with("std::")) {
        return None;
    }
    let name = tcx.item_name(def_id).to_string();
    let container = match name.as_str() {
        "Option" => "option",
        "Box" => "box",
        "String" => "string",
        "Vec" => "vec",
        _ => return None,
    };
    let element = if name == "String" {
        String::new()
    } else {
        args.types().next().map(|ty| type_id(tcx, ty)).unwrap_or_default()
    };
    Some((container, element))
}

// Record standard containers that need an explicit ABI contract. The helper
// never assumes their niche layout or ownership; it only reports the fact.
fn collect_standard_containers<'tcx>(
    tcx: TyCtxt<'tcx>,
    owner: &str,
    context: &str,
    ty: Ty<'tcx>,
    out: &mut Vec<Value>,
    seen: &mut BTreeSet<String>,
) {
    if let Some((container, element)) = standard_container(tcx, ty) {
        let key = format!("{}|{}|{}|{}", owner, context, container, element);
        if seen.insert(key) {
            out.push(json!({
                "owner": owner,
                "context": context,
                "type": type_id(tcx, ty),
                "container": container,
                "element": if element.is_empty() { Value::Null } else { json!(element) },
            }));
        }
        if let TyKind::Adt(_, args) = ty.kind() {
            for argument in args.types() {
                collect_standard_containers(tcx, owner, context, argument, out, seen);
            }
        }
        return;
    }
    match ty.kind() {
        TyKind::Adt(adt, args) => {
            for field in adt.all_fields() {
                let field_ty = tcx.type_of(field.did).instantiate(tcx, args).skip_normalization();
                collect_standard_containers(tcx, owner, context, field_ty, out, seen);
            }
        }
        TyKind::RawPtr(pointee, _) | TyKind::Ref(_, pointee, _) | TyKind::Slice(pointee) => {
            collect_standard_containers(tcx, owner, context, *pointee, out, seen);
        }
        TyKind::Array(element, _) => collect_standard_containers(tcx, owner, context, *element, out, seen),
        TyKind::Tuple(elements) => {
            for element in elements.iter() {
                collect_standard_containers(tcx, owner, context, element, out, seen);
            }
        }
        _ => {}
    }
}

fn primitive_ty<'tcx>(tcx: TyCtxt<'tcx>, name: &str) -> Option<Ty<'tcx>> {
    Some(match name {
        "bool" => tcx.types.bool,
        "char" => tcx.types.char,
        "i8" => tcx.types.i8,
        "i16" => tcx.types.i16,
        "i32" => tcx.types.i32,
        "i64" => tcx.types.i64,
        "i128" => tcx.types.i128,
        "isize" => tcx.types.isize,
        "u8" => tcx.types.u8,
        "u16" => tcx.types.u16,
        "u32" => tcx.types.u32,
        "u64" => tcx.types.u64,
        "u128" => tcx.types.u128,
        "usize" => tcx.types.usize,
        "f32" => tcx.types.f32,
        "f64" => tcx.types.f64,
        "str" => tcx.types.str_,
        _ => return None,
    })
}

fn parse_instance_spec(spec: &str) -> Option<(String, Vec<String>)> {
    let (name, arguments) = spec.split_once('=')?;
    let arguments = arguments
        .split(',')
        .map(str::trim)
        .filter(|argument| !argument.is_empty())
        .map(str::to_string)
        .collect();
    Some((name.trim().to_string(), arguments))
}

// Resolve one explicit type argument: a primitive or a non-generic local type.
// Arbitrary paths and generic types are intentionally rejected so the helper
// never guesses a monomorphization the user did not spell out.
fn instance_type_argument<'tcx>(tcx: TyCtxt<'tcx>, name: &str, types: &BTreeMap<String, DefId>) -> Option<Ty<'tcx>> {
    if let Some(ty) = primitive_ty(tcx, name) {
        return Some(ty);
    }
    let def_id = types.get(name).copied()?;
    if !matches!(
        tcx.def_kind(def_id),
        DefKind::Struct | DefKind::Union | DefKind::Enum | DefKind::TyAlias
    ) {
        return None;
    }
    let generics = tcx.generics_of(def_id);
    if generics
        .own_params
        .iter()
        .any(|parameter| !matches!(parameter.kind, GenericParamDefKind::Lifetime))
    {
        return None;
    }
    Some(tcx.type_of(def_id).instantiate_identity().skip_normalization())
}

// Resolve one const argument for the declared const parameter type. Decimal and
// negative integer literals are supported; expressions are not evaluated.
fn instance_const_argument<'tcx>(
    tcx: TyCtxt<'tcx>,
    parameter: &rustc_middle::ty::GenericParamDef,
    text: &str,
) -> Option<rustc_middle::ty::Const<'tcx>> {
    let (negative, digits) = match text.strip_prefix('-') {
        Some(rest) => (true, rest),
        None => (false, text),
    };
    let value: u128 = digits.parse().ok()?;
    let value = if negative {
        (value as i128).wrapping_neg() as u128
    } else {
        value
    };
    let ty = tcx
        .type_of(parameter.def_id)
        .instantiate_identity()
        .skip_normalization();
    Some(rustc_middle::ty::Const::from_bits(
        tcx,
        value,
        TypingEnv::fully_monomorphized(),
        ty,
    ))
}

fn instance_generic_args<'tcx>(
    tcx: TyCtxt<'tcx>,
    def_id: DefId,
    arguments: &[String],
    types: &BTreeMap<String, DefId>,
) -> Option<Vec<GenericArg<'tcx>>> {
    let generics = tcx.generics_of(def_id);
    let mut supplied = arguments.iter();
    let mut args = Vec::with_capacity(generics.own_params.len());
    for parameter in &generics.own_params {
        match parameter.kind {
            GenericParamDefKind::Lifetime => args.push(tcx.lifetimes.re_erased.into()),
            GenericParamDefKind::Type { .. } => {
                let text = supplied.next()?;
                args.push(instance_type_argument(tcx, text, types)?.into());
            }
            GenericParamDefKind::Const { .. } => {
                let text = supplied.next()?;
                args.push(instance_const_argument(tcx, parameter, text)?.into());
            }
        }
    }
    if supplied.next().is_some() {
        return None;
    }
    Some(args)
}

struct HelperCallbacks {
    output: Option<PathBuf>,
    source: PathBuf,
    rustc: String,
    cargo: Option<CargoFacts>,
    features: Vec<String>,
    instantiate: Vec<String>,
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

        let mut types: BTreeMap<String, Value> = BTreeMap::new();
        let mut functions: BTreeMap<String, Value> = BTreeMap::new();
        let mut traits: BTreeMap<String, Value> = BTreeMap::new();
        let mut impls: BTreeMap<String, Value> = BTreeMap::new();
        let mut generic_items: BTreeMap<String, DefId> = BTreeMap::new();
        let mut generic_defs: BTreeMap<String, DefId> = BTreeMap::new();
        let mut instances: BTreeMap<String, Value> = BTreeMap::new();
        let mut instances_by_decl: BTreeMap<String, BTreeSet<String>> = BTreeMap::new();
        let mut seen_instances: BTreeSet<String> = BTreeSet::new();
        let mut errors: BTreeMap<String, (Ty<'_>, BTreeSet<String>)> = BTreeMap::new();
        let mut lifetimes: Vec<Value> = Vec::new();
        let mut trait_objects: Vec<Value> = Vec::new();
        let mut seen_trait_objects: BTreeSet<String> = BTreeSet::new();
        let mut async_functions: Vec<Value> = Vec::new();
        let mut containers: Vec<Value> = Vec::new();
        let mut seen_containers: BTreeSet<String> = BTreeSet::new();

        for free_item_id in tcx.hir_free_items() {
            let item = tcx.hir_item(free_item_id);
            let def_id = item.owner_id.def_id.to_def_id();
            match item.kind {
                ItemKind::Struct(..) | ItemKind::Union(..) | ItemKind::Enum(..) | ItemKind::TyAlias(..) => {
                    generic_items.insert(item_id(tcx, def_id), def_id);
                    generic_defs.insert(path_string(tcx, def_id), def_id);
                    generic_defs.insert(tcx.item_name(def_id).to_string(), def_id);
                    if let Some((id, value)) = emit_type(tcx, def_id) {
                        types.insert(id, value);
                    }
                    let ty = tcx.type_of(def_id).instantiate_identity().skip_normalization();
                    collect_type_instances(tcx, ty, &mut instances, &mut instances_by_decl, &mut seen_instances);
                    collect_lifetimes(tcx, def_id, &mut lifetimes);
                    let owner = item_id(tcx, def_id);
                    if let TyKind::Adt(adt, args) = ty.kind() {
                        for field in adt.all_fields() {
                            let field_ty = tcx.type_of(field.did).instantiate(tcx, args).skip_normalization();
                            collect_trait_objects(
                                tcx,
                                &owner,
                                "field",
                                field_ty,
                                &mut trait_objects,
                                &mut seen_trait_objects,
                            );
                            collect_standard_containers(
                                tcx,
                                &owner,
                                "field",
                                field_ty,
                                &mut containers,
                                &mut seen_containers,
                            );
                        }
                    } else {
                        collect_trait_objects(tcx, &owner, "alias", ty, &mut trait_objects, &mut seen_trait_objects);
                        collect_standard_containers(tcx, &owner, "alias", ty, &mut containers, &mut seen_containers);
                    }
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
                    collect_lifetimes(tcx, def_id, &mut lifetimes);
                    let owner = item_id(tcx, def_id);
                    let signature = tcx.fn_sig(def_id).skip_binder();
                    for input in signature.inputs().skip_binder() {
                        collect_trait_objects(
                            tcx,
                            &owner,
                            "parameter",
                            *input,
                            &mut trait_objects,
                            &mut seen_trait_objects,
                        );
                        collect_standard_containers(
                            tcx,
                            &owner,
                            "parameter",
                            *input,
                            &mut containers,
                            &mut seen_containers,
                        );
                    }
                    collect_trait_objects(
                        tcx,
                        &owner,
                        "return",
                        signature.output().skip_binder(),
                        &mut trait_objects,
                        &mut seen_trait_objects,
                    );
                    collect_standard_containers(
                        tcx,
                        &owner,
                        "return",
                        signature.output().skip_binder(),
                        &mut containers,
                        &mut seen_containers,
                    );
                    if tcx.asyncness(def_id).is_async() {
                        async_functions.push(json!({
                            "id": owner,
                            "path": path_string(tcx, def_id),
                            "name": tcx.item_name(def_id).to_string(),
                            "return_type": type_id(tcx, signature.output().skip_binder()),
                        }));
                    }
                }
                ItemKind::Trait { .. } => {
                    generic_items.insert(item_id(tcx, def_id), def_id);
                    if let Some(value) = emit_trait(tcx, def_id) {
                        traits.insert(item_id(tcx, def_id), value);
                    }
                    collect_lifetimes(tcx, def_id, &mut lifetimes);
                    for associated in tcx.associated_items(def_id).in_definition_order() {
                        if !associated.is_fn() {
                            continue;
                        }
                        collect_function_metadata(
                            tcx,
                            associated.def_id,
                            &mut lifetimes,
                            &mut trait_objects,
                            &mut seen_trait_objects,
                            &mut async_functions,
                            &mut containers,
                            &mut seen_containers,
                        );
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
                    collect_lifetimes(tcx, def_id, &mut lifetimes);
                    for associated in tcx.associated_items(def_id).in_definition_order() {
                        if associated.is_fn() {
                            collect_function_metadata(
                                tcx,
                                associated.def_id,
                                &mut lifetimes,
                                &mut trait_objects,
                                &mut seen_trait_objects,
                                &mut async_functions,
                                &mut containers,
                                &mut seen_containers,
                            );
                        }
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
        for spec in &self.instantiate {
            let Some((name, arguments)) = parse_instance_spec(spec) else {
                continue;
            };
            let Some(def_id) = generic_defs.get(&name).copied() else {
                continue;
            };
            if !matches!(tcx.def_kind(def_id), DefKind::Struct | DefKind::Union | DefKind::Enum) {
                continue;
            }
            let Some(generic_args) = instance_generic_args(tcx, def_id, &arguments, &generic_defs) else {
                continue;
            };
            let adt = tcx.adt_def(def_id);
            let ty = Ty::new_adt(tcx, adt, tcx.mk_args(&generic_args));
            collect_type_instances(tcx, ty, &mut instances, &mut instances_by_decl, &mut seen_instances);
        }
        for (id, value) in instances {
            types.entry(id).or_insert(value);
        }

        let mut generics: BTreeMap<String, Value> = BTreeMap::new();
        for (id, def_id) in generic_items {
            if let Some(value) = emit_generic(tcx, def_id, &instances_by_decl) {
                generics.insert(id, value);
            }
        }

        let mut error_documents: BTreeMap<String, Value> = BTreeMap::new();
        for (id, (ty, names)) in errors {
            let names: Vec<String> = names.into_iter().collect();
            if let Some(value) = emit_error(tcx, ty, &names) {
                error_documents.insert(id, value);
            }
        }

        let cargo_name = self
            .cargo
            .as_ref()
            .map(|cargo| cargo.name.clone())
            .unwrap_or_else(|| crate_name.clone());
        // Report the edition rustc actually compiled with, not the Cargo
        // manifest default that may have been overridden by `--edition`.
        let cargo_edition = edition.clone();
        let features = self
            .cargo
            .as_ref()
            .map(|cargo| cargo.features.clone())
            .unwrap_or_else(|| self.features.clone());
        let cargo_json = match &self.cargo {
            None => Value::Null,
            Some(cargo) => json!({
                "package_id": cargo.package_id,
                "manifest_path": cargo.manifest_path,
                "workspace_root": cargo.workspace_root,
                "target_directory": cargo.target_directory,
                "workspace_members": cargo.workspace_members,
                "default_members": cargo.default_members,
                "edition": cargo.edition,
                "dependencies": cargo.dependencies.iter().map(|dependency| json!({
                    "name": dependency.name,
                    "requirement": dependency.requirement,
                    "kind": dependency.kind,
                    "optional": dependency.optional,
                    "path": dependency.path,
                })).collect::<Vec<_>>(),
            }),
        };

        let document = json!({
            "schema": SCHEMA,
            "producer": {
                "kind": "rustc-helper",
                "version": HELPER_VERSION,
                "rustc": self.rustc,
            },
            "target": {
                "triple": triple,
                "arch": arch,
                "os": os,
                "abi": abi,
                "pointer_bits": pointer_bits,
                "endianness": endianness,
                "c_long_bits": c_long_bits,
            },
            "crate": {
                "name": cargo_name,
                "edition": cargo_edition,
                "features": features,
            },
            "cargo": cargo_json,
            "types": types.into_values().collect::<Vec<_>>(),
            "functions": functions.into_values().collect::<Vec<_>>(),
            "generics": generics.into_values().collect::<Vec<_>>(),
            "traits": traits.into_values().chain(impls.into_values()).collect::<Vec<_>>(),
            "errors": error_documents.into_values().collect::<Vec<_>>(),
            "lifetimes": lifetimes,
            "trait_objects": trait_objects,
            "async_functions": async_functions,
            "containers": containers,
            "provenance": {
                "source": self.source.to_string_lossy(),
                "rustc": self.rustc,
            },
        });
        let text = serde_json::to_string_pretty(&document).unwrap_or_else(|_| "{}".to_string());
        match &self.output {
            None => println!("{}", text),
            Some(path) if path.as_os_str() == "-" => println!("{}", text),
            Some(path) => {
                if let Err(error) = fs::write(path, text + "\n") {
                    eprintln!("amc-rustc-helper: cannot write {}: {}", path.display(), error);
                }
            }
        }
        Compilation::Stop
    }
}

fn main() {
    let cli = Cli::parse();

    // rustc_private is an unstable interface even on a stable toolchain. The
    // helper is pinned to a rustc version and runs as an isolated process.
    unsafe {
        env::set_var("RUSTC_BOOTSTRAP", "1");
    }

    let cargo = cargo_facts(&cli);
    let edition = cli
        .edition
        .clone()
        .or_else(|| cargo.as_ref().map(|cargo| cargo.edition.clone()))
        .unwrap_or_else(|| "2021".to_string());
    let metadata = env::temp_dir().join(format!("amc-rustc-helper-{}.rmeta", std::process::id()));
    let metadata_path = metadata.to_string_lossy().into_owned();
    let mut args = vec![
        "rustc".to_string(),
        "--crate-name".to_string(),
        cli.crate_name.clone(),
        "--edition".to_string(),
        edition,
        "--crate-type=lib".to_string(),
        "--emit=metadata".to_string(),
        "-o".to_string(),
        metadata_path.clone(),
    ];
    if let Some(target) = &cli.target {
        args.push("--target".to_string());
        args.push(target.clone());
    }
    args.push(cli.source.to_string_lossy().into_owned());

    let mut callbacks = HelperCallbacks {
        output: cli.output,
        source: cli.source,
        rustc: rustc_release(),
        cargo,
        features: cli.features,
        instantiate: cli.instantiate,
    };
    rustc_driver::run_compiler(&args, &mut callbacks);
    let _ = fs::remove_file(&metadata);
}
