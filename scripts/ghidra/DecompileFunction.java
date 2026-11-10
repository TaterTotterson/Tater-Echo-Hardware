// Decompile exactly one named function or address from an analyzed project.
// @category Tater

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.List;

public class DecompileFunction extends GhidraScript {
    private static String json(String value) {
        if (value == null) return "";
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < value.length(); i++) {
            char ch = value.charAt(i);
            switch (ch) {
                case '\\': out.append("\\\\"); break;
                case '"': out.append("\\\""); break;
                case '\b': out.append("\\b"); break;
                case '\f': out.append("\\f"); break;
                case '\n': out.append("\\n"); break;
                case '\r': out.append("\\r"); break;
                case '\t': out.append("\\t"); break;
                default:
                    if (ch < 0x20) out.append(String.format("\\u%04x", (int) ch));
                    else out.append(ch);
            }
        }
        return out.toString();
    }

    private Function findFunction(String selector) {
        try {
            Address address = currentProgram.getAddressFactory().getAddress(selector);
            if (address != null) {
                Function function = currentProgram.getFunctionManager().getFunctionAt(address);
                if (function == null) {
                    function = currentProgram.getFunctionManager().getFunctionContaining(address);
                }
                if (function != null) return function;
            }
        } catch (Exception ignored) {
            // Treat a non-address selector as a name.
        }
        List<Function> matches = getGlobalFunctions(selector);
        if (matches.size() == 1) return matches.get(0);
        if (matches.size() > 1) {
            throw new IllegalArgumentException("function name is ambiguous: " + selector);
        }
        for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
            if (function.getName(true).equals(selector)) return function;
        }
        throw new IllegalArgumentException("function was not found: " + selector);
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) {
            throw new IllegalArgumentException("DecompileFunction.java requires selector and output path");
        }
        Function function = findFunction(args[0]);
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("decompiler could not open the program");
        }
        DecompileResults result = decompiler.decompileFunction(function, 120, monitor);
        if (!result.decompileCompleted() || result.getDecompiledFunction() == null) {
            throw new IllegalStateException("decompilation failed: " + result.getErrorMessage());
        }
        String code = result.getDecompiledFunction().getC();
        try (PrintWriter output = new PrintWriter(new OutputStreamWriter(
                new FileOutputStream(args[1]), StandardCharsets.UTF_8))) {
            output.println("{\"address\":\"" + json(function.getEntryPoint().toString())
                    + "\",\"name\":\"" + json(function.getName(true))
                    + "\",\"signature\":\"" + json(function.getSignature().toString())
                    + "\",\"decompiled\":\"" + json(code) + "\"}");
        } finally {
            decompiler.dispose();
        }
    }
}

