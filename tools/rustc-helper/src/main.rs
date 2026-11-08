// amc-rustc-helper — minimal v0.4.0 Rust semantic adapter producer.
//
// This skeleton establishes the process boundary and the stable
// `amc.rust.semantic/1` envelope. It reports the target and crate facts from
// the installed rustc and leaves types/functions empty for now; later revisions
// fill them from rustc's semantic APIs without changing the schema envelope.
//
// The helper intentionally has no external crate dependencies so it can be
// built with a plain `rustc` invocation and shipped next to `amc-rust`.

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
}

fn usage() -> String {
    "usage: amc-rustc-helper --crate-name <name> [--edition <edition>] \
     [--source <path>] [--output <path>]\n\
     \n\
     Emits one amc.rust.semantic/1 document on stdout when --output is omitted."
        .to_string()
}

fn parse_options() -> Result<Options, String> {
    let mut crate_name = String::new();
    let mut edition = String::from("2021");
    let mut source = String::new();
    let mut output = String::new();
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
            _ => return Err(format!("unknown option: {}", flag)),
        }
        index += 2;
    }
    if crate_name.is_empty() {
        return Err("--crate-name is required".to_string());
    }
    Ok(Options {
        crate_name,
        edition,
        source,
        output,
    })
}

fn run_rustc(args: &[&str]) -> Result<String, String> {
    let output = Command::new("rustc")
        .args(args)
        .stdin(Stdio::null())
        .output()
        .map_err(|error| format!("cannot run rustc: {}", error))?;
    if !output.status.success() {
        return Err(format!(
            "rustc {} failed: {}",
            args.join(" "),
            String::from_utf8_lossy(&output.stderr).trim()
        ));
    }
    Ok(String::from_utf8_lossy(&output.stdout).into_owned())
}

fn cfg_value<'a>(cfg: &'a str, key: &str) -> Option<&'a str> {
    for line in cfg.lines() {
        let line = line.trim();
        if let Some(rest) = line.strip_prefix(key) {
            let rest = rest.trim_start();
            if let Some(rest) = rest.strip_prefix('=') {
                return Some(rest.trim().trim_matches('"'));
            }
        }
    }
    None
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

fn main() {
    let options = match parse_options() {
        Ok(options) => options,
        Err(error) => {
            eprintln!("amc-rustc-helper: {}", error);
            eprintln!("{}", usage());
            std::process::exit(2);
        }
    };

    let cfg = match run_rustc(&["--print", "cfg"]) {
        Ok(cfg) => cfg,
        Err(error) => {
            eprintln!("amc-rustc-helper: {}", error);
            std::process::exit(3);
        }
    };
    let verbose = match run_rustc(&["-vV"]) {
        Ok(verbose) => verbose,
        Err(error) => {
            eprintln!("amc-rustc-helper: {}", error);
            std::process::exit(3);
        }
    };

    let mut host = String::new();
    let mut release = String::new();
    for line in verbose.lines() {
        if let Some(value) = line.strip_prefix("host: ") {
            host = value.trim().to_string();
        } else if let Some(value) = line.strip_prefix("release: ") {
            release = value.trim().to_string();
        }
    }
    if host.is_empty() {
        eprintln!("amc-rustc-helper: rustc -vV did not report a host triple");
        std::process::exit(3);
    }

    let arch = cfg_value(&cfg, "target_arch").unwrap_or("unknown");
    let os = cfg_value(&cfg, "target_os").unwrap_or("unknown");
    let endianness = cfg_value(&cfg, "target_endian").unwrap_or("unknown");
    let pointer_bits = cfg_value(&cfg, "target_pointer_width").unwrap_or("0");
    let abi = host.rsplit('-').next().unwrap_or("unknown");
    // Rust's cfg surface does not expose c_long directly. Windows is LLP64;
    // other supported targets follow the pointer width. AMC can override the
    // value from [target] c_long_bits when the target is unusual.
    let c_long_bits = if os == "windows" {
        "32".to_string()
    } else {
        pointer_bits.to_string()
    };

    let target = format!(
        "{{\n    \"triple\": {},\n    \"arch\": {},\n    \"os\": {},\n    \"abi\": {},\n    \"pointer_bits\": {},\n    \"endianness\": {},\n    \"c_long_bits\": {}\n  }}",
        json_string(&host),
        json_string(arch),
        json_string(os),
        json_string(abi),
        pointer_bits,
        json_string(endianness),
        c_long_bits
    );
    let crate_section = format!(
        "{{\n    \"name\": {},\n    \"edition\": {},\n    \"features\": []\n  }}",
        json_string(&options.crate_name),
        json_string(&options.edition)
    );
    let producer = format!(
        "{{\n    \"kind\": \"rustc-helper\",\n    \"version\": {},\n    \"rustc\": {}\n  }}",
        json_string(HELPER_VERSION),
        json_string(&release)
    );
    let provenance = format!(
        "{{\n    \"source\": {},\n    \"rustc\": {}\n  }}",
        json_string(&options.source),
        json_string(&release)
    );

    let document = format!(
        "{{\n  \"schema\": {},\n  \"producer\": {},\n  \"target\": {},\n  \"crate\": {},\n  \"types\": [],\n  \"functions\": [],\n  \"generics\": [],\n  \"traits\": [],\n  \"errors\": [],\n  \"provenance\": {}\n}}\n",
        json_string(SCHEMA),
        producer,
        target,
        crate_section,
        provenance
    );

    if options.output.is_empty() || options.output == "-" {
        print!("{}", document);
    } else if let Err(error) = fs::write(&options.output, document) {
        eprintln!("amc-rustc-helper: cannot write {}: {}", options.output, error);
        std::process::exit(4);
    }
}
