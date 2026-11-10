// Export stable, machine-readable program metadata after Ghidra analysis.
// @category Tater

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.util.DefinedDataIterator;

import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.Set;

public class ExportProgram extends GhidraScript {
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

    private static String quoted(String value) {
        return "\"" + json(value) + "\"";
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("ExportProgram.java requires an output path");
        }
        try (PrintWriter output = new PrintWriter(new OutputStreamWriter(
                new FileOutputStream(args[0]), StandardCharsets.UTF_8))) {
            output.println("{\"record\":\"program\",\"name\":" + quoted(currentProgram.getName())
                    + ",\"executable_sha256\":" + quoted(currentProgram.getExecutableSHA256())
                    + ",\"language\":" + quoted(currentProgram.getLanguageID().toString())
                    + ",\"compiler\":" + quoted(currentProgram.getCompilerSpec().getCompilerSpecID().toString())
                    + ",\"image_base\":" + quoted(currentProgram.getImageBase().toString()) + "}");

            for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
                monitor.checkCancelled();
                StringBuilder calls = new StringBuilder("[");
                boolean first = true;
                Set<Function> called = function.getCalledFunctions(monitor);
                for (Function target : called) {
                    if (!first) calls.append(',');
                    calls.append("{\"address\":").append(quoted(target.getEntryPoint().toString()))
                            .append(",\"name\":").append(quoted(target.getName())).append('}');
                    first = false;
                }
                calls.append(']');
                Symbol symbol = function.getSymbol();
                output.println("{\"record\":\"function\",\"address\":"
                        + quoted(function.getEntryPoint().toString())
                        + ",\"name\":" + quoted(function.getName())
                        + ",\"namespace\":" + quoted(symbol.getParentNamespace().getName(true))
                        + ",\"signature\":" + quoted(function.getSignature().toString())
                        + ",\"external\":" + function.isExternal()
                        + ",\"thunk\":" + function.isThunk()
                        + ",\"calls\":" + calls + "}");
            }

            for (Data data : DefinedDataIterator.byDataInstance(
                    currentProgram, candidate -> candidate.hasStringValue())) {
                monitor.checkCancelled();
                Object value = data.getValue();
                String text = value == null ? "" : value.toString();
                if (text.length() > 4096) text = text.substring(0, 4096);
                output.println("{\"record\":\"string\",\"address\":"
                        + quoted(data.getAddress().toString())
                        + ",\"value\":" + quoted(text) + "}");
            }
        }
    }
}

