// Static evidence export; never executes the imported program.
// Optional define:ADDRESS requests annotate analyst-confirmed entrypoints in the project only.
// @category CitroenC4
import ghidra.app.util.headless.HeadlessScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public class ExportEvidence extends HeadlessScript {
    private Path out;
    private String cell(Object value) {
        return String.valueOf(value).replace("\\", "\\\\").replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n");
    }
    private PrintWriter table(String name, String header) throws IOException {
        PrintWriter writer = new PrintWriter(Files.newBufferedWriter(out.resolve(name), StandardCharsets.UTF_8));
        writer.println(header);
        return writer;
    }
    private void row(PrintWriter writer, Object... values) {
        for (int i = 0; i < values.length; i++) {
            if (i != 0) writer.print('\t');
            writer.print(cell(values[i]));
        }
        writer.println();
    }
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) throw new IllegalArgumentException("ExportEvidence.java OUTPUT [ENTRY_ADDRESS | define:ADDRESS ...] (define: requires an analyst-confirmed entrypoint)");
        out = Paths.get(args[0]);
        Files.createDirectories(out.resolve("decompiled"));
        FunctionManager fm = currentProgram.getFunctionManager();
        try (PrintWriter defined = table("defined-functions.tsv", "address\tstatus\tname")) {
            for (int i = 1; i < args.length; i++) {
                if (!args[i].startsWith("define:")) continue;
                monitor.checkCancelled();
                Address address = toAddr(args[i].substring("define:".length()));
                ghidra.program.model.mem.MemoryBlock block = currentProgram.getMemory().getBlock(address);
                if (block == null || !block.isExecute())
                    throw new IllegalArgumentException("Confirmed entrypoint is not in executable memory: " + address);
                Function f = fm.getFunctionAt(address);
                String status = "existing";
                if (f == null) {
                    if (fm.getFunctionContaining(address) != null)
                        throw new IllegalArgumentException("Confirmed entrypoint is inside an existing function: " + address);
                    if (getInstructionAt(address) == null && !disassemble(address))
                        throw new IOException("Could not disassemble confirmed entrypoint: " + address);
                    f = createFunction(address, null);
                    if (f == null) throw new IOException("Could not define confirmed entrypoint: " + address);
                    String comment = currentProgram.getListing().getComment(CodeUnit.PLATE_COMMENT, address);
                    currentProgram.getListing().setComment(address, CodeUnit.PLATE_COMMENT,
                        (comment == null ? "" : comment + "\n") +
                        "Analyst-confirmed entrypoint explicitly defined for static evidence export.");
                    status = "defined";
                }
                row(defined, address, status, f.getName(true));
                args[i] = address.toString();
            }
        }
        SymbolTable symbols = currentProgram.getSymbolTable();
        ReferenceManager refs = currentProgram.getReferenceManager();
        long functions = 0, calls = 0, strings = 0, imports = 0, exports = 0;
        try (PrintWriter inventory = table("functions.tsv", "address\tname\tsignature\tbody_bytes\tthunk\texternal");
             PrintWriter edges = table("calls.tsv", "caller_address\tcallee_address\tcallee_name")) {
            FunctionIterator iterator = fm.getFunctions(true);
            while (iterator.hasNext()) {
                monitor.checkCancelled();
                Function f = iterator.next();
                row(inventory, f.getEntryPoint(), f.getName(true), f.getSignature(), f.getBody().getNumAddresses(), f.isThunk(), f.isExternal());
                functions++;
                for (Function callee : f.getCalledFunctions(monitor)) {
                    row(edges, f.getEntryPoint(), callee.getEntryPoint(), callee.getName(true));
                    calls++;
                }
            }
        }
        try (PrintWriter external = table("imports.tsv", "address\tlibrary\tname\tsymbol_type");
             PrintWriter exported = table("exports.tsv", "address\tnames")) {
            SymbolIterator iterator = symbols.getExternalSymbols();
            while (iterator.hasNext()) {
                Symbol s = iterator.next();
                ExternalLocation loc = currentProgram.getExternalManager().getExternalLocation(s);
                row(external, s.getAddress(), loc == null ? "" : loc.getLibraryName(), s.getName(), s.getSymbolType());
                imports++;
            }
            AddressIterator entries = symbols.getExternalEntryPointIterator();
            while (entries.hasNext()) {
                Address address = entries.next();
                List<String> names = new ArrayList<>();
                for (Symbol s : symbols.getSymbols(address)) names.add(s.getName(true));
                row(exported, address, String.join(";", names));
                exports++;
            }
        }
        try (PrintWriter data = table("strings.tsv", "address\tlength\tvalue");
             PrintWriter xrefs = table("string_xrefs.tsv", "string_address\tfrom_address\tfunction_address\tfunction_name\treference_type")) {
            DataIterator iterator = currentProgram.getListing().getDefinedData(true);
            while (iterator.hasNext()) {
                monitor.checkCancelled();
                Data d = iterator.next();
                if (!StringDataInstance.isString(d)) continue;
                String value = StringDataInstance.getStringDataInstance(d).getStringValue();
                if (value == null) continue;
                row(data, d.getAddress(), d.getLength(), value);
                strings++;
                ReferenceIterator references = refs.getReferencesTo(d.getAddress());
                while (references.hasNext()) {
                    Reference r = references.next();
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    row(xrefs, d.getAddress(), r.getFromAddress(), f == null ? "" : f.getEntryPoint(), f == null ? "" : f.getName(true), r.getReferenceType());
                }
            }
        }
        try (PrintWriter syms = table("symbols.tsv", "address\tname\ttype\tsource");
             PrintWriter references = table("data_refs.tsv", "function_address\tfrom_address\tto_address\ttype")) {
            SymbolIterator iterator = symbols.getAllSymbols(false);
            while (iterator.hasNext()) {
                Symbol s = iterator.next();
                row(syms, s.getAddress(), s.getName(true), s.getSymbolType(), s.getSource());
            }
            FunctionIterator fs = fm.getFunctions(true);
            while (fs.hasNext()) {
                Function f = fs.next();
                InstructionIterator insns = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (insns.hasNext()) {
                    for (Reference r : insns.next().getReferencesFrom()) {
                        if (r.getReferenceType().isData()) row(references, f.getEntryPoint(), r.getFromAddress(), r.getToAddress(), r.getReferenceType());
                    }
                }
            }
        }
        try (PrintWriter fp = table("fp_instructions.tsv", "address\tmnemonic\tfunction_entry\tfunction_name\tinstruction");
             PrintWriter fields = table("entity_field_references.tsv", "address\tmnemonic\tfunction_entry\tfunction_name\tinstruction\toperand_index");
             PrintWriter subfields = table("entity_private_subfield_references.tsv", "address\tmnemonic\tfunction_entry\tfunction_name\tinstruction\toperand_index\tmatched_offset")) {
            InstructionIterator iterator = currentProgram.getListing().getInstructions(true);
            while (iterator.hasNext()) {
                monitor.checkCancelled();
                Instruction instruction = iterator.next();
                String mnemonic = instruction.getMnemonicString();
                // Scalar offset matches only: MATERIAL also has +0x238; no entity type,
                // read/write, mask, lifetime, or runtime execution is established.
                for (int operand = 0; operand < instruction.getNumOperands(); operand++) {
                    Object[] operandObjects = instruction.getOpObjects(operand);
                    for (Object object : operandObjects) {
                        if (!(object instanceof ghidra.program.model.scalar.Scalar) ||
                            ((ghidra.program.model.scalar.Scalar) object).getUnsignedValue() != 0x238L)
                            continue;
                        Function f = fm.getFunctionContaining(instruction.getAddress());
                        row(fields, instruction.getAddress(), mnemonic, f == null ? "" : f.getEntryPoint(),
                            f == null ? "" : f.getName(true), instruction.toString(), operand);
                        break;
                    }
                    // Byte/word subfield matches only, not ENTITY typing, writer, mask,
                    // or lifetime proof.
                    int matchedSubfields = 0;
                    for (Object object : operandObjects) {
                        if (!(object instanceof ghidra.program.model.scalar.Scalar)) continue;
                        long offset = ((ghidra.program.model.scalar.Scalar) object).getUnsignedValue();
                        if (offset < 0x239L || offset > 0x23bL) continue;
                        int bit = 1 << (int) (offset - 0x239L);
                        if ((matchedSubfields & bit) != 0) continue;
                        matchedSubfields |= bit;
                        Function f = fm.getFunctionContaining(instruction.getAddress());
                        row(subfields, instruction.getAddress(), mnemonic, f == null ? "" : f.getEntryPoint(),
                            f == null ? "" : f.getName(true), instruction.toString(), operand,
                            "0x" + Long.toHexString(offset));
                    }
                }
                switch (mnemonic) {
                    case "FLDCW": case "FLDENV": case "FRSTOR": case "FXRSTOR":
                    case "XRSTOR": case "XRSTORS": case "FINIT": case "FNINIT":
                    case "LDMXCSR": case "FNSTCW": case "FSTCW": case "FNSTENV":
                    case "FSTENV": case "FSAVE": case "FNSAVE": case "FXSAVE":
                    case "XSAVE": case "XSAVEOPT": case "XSAVEC": case "XSAVES":
                    case "STMXCSR":
                        break;
                    default:
                        continue;
                }
                Function f = fm.getFunctionContaining(instruction.getAddress());
                row(fp, instruction.getAddress(), mnemonic, f == null ? "" : f.getEntryPoint(),
                    f == null ? "" : f.getName(true), instruction.toString());
            }
        }
        try (PrintWriter blocks = table("memory.tsv", "name\tstart\tend\tread\twrite\texecute");
             PrintWriter summary = table("summary.tsv", "key\tvalue")) {
            for (ghidra.program.model.mem.MemoryBlock b : currentProgram.getMemory().getBlocks())
                row(blocks, b.getName(), b.getStart(), b.getEnd(), b.isRead(), b.isWrite(), b.isExecute());
            row(summary, "program", currentProgram.getName());
            row(summary, "executable_path", currentProgram.getExecutablePath());
            row(summary, "sha256", currentProgram.getExecutableSHA256());
            row(summary, "language", currentProgram.getLanguageID());
            row(summary, "compiler", currentProgram.getCompilerSpec().getCompilerSpecID());
            row(summary, "image_base", currentProgram.getImageBase());
            row(summary, "analysis_timed_out", analysisTimeoutOccurred());
            row(summary, "functions", functions); row(summary, "calls", calls);
            row(summary, "strings", strings); row(summary, "imports", imports); row(summary, "exports", exports);
        }
        DecompInterface decompiler = new DecompInterface();
        try (PrintWriter selected = table("decompilation.tsv", "address\tname\tstatus\tfile\terror")) {
            if (!decompiler.openProgram(currentProgram)) throw new IOException(decompiler.getLastMessage());
            for (int i = 1; i < args.length; i++) {
                Address address = toAddr(args[i]);
                Function f = fm.getFunctionAt(address);
                if (f == null) { row(selected, address, "", "missing_function", "", ""); continue; }
                DecompileResults result = decompiler.decompileFunction(f, 60, monitor);
                String name = "decompiled/" + address.toString().replace(':', '_') + ".c";
                if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                    Files.writeString(out.resolve(name), result.getDecompiledFunction().getC(), StandardCharsets.UTF_8);
                    row(selected, address, f.getName(true), "complete", name, "");
                } else row(selected, address, f.getName(true), "failed", "", result.getErrorMessage());
            }
        } finally { decompiler.dispose(); }
        Files.writeString(out.resolve("export-complete.txt"), currentProgram.getExecutableSHA256() + "\n", StandardCharsets.UTF_8);
        println("Evidence exported to " + out);
    }
}
