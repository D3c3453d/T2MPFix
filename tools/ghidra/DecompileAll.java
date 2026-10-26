// Headless: decompile every function into <out>/decomp.c, write <out>/functions.tsv
// (entry, size, name) and <out>/strings.tsv (string address, text, referencing functions).
// Usage: analyzeHeadless ... -postScript DecompileAll.java <outdir>
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class DecompileAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "/out";
        new File(out).mkdirs();
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        try (PrintWriter idx = new PrintWriter(new FileWriter(out + "/functions.tsv"))) {
            for (Function f : fm.getFunctions(true))
                idx.printf("%s\t%d\t%s%n", f.getEntryPoint(), f.getBody().getNumAddresses(), f.getName());
        }

        try (PrintWriter sw = new PrintWriter(new FileWriter(out + "/strings.tsv"))) {
            ReferenceManager rm = currentProgram.getReferenceManager();
            for (Data d : listing.getDefinedData(true)) {
                if (!d.hasStringValue()) continue;
                String s = StringDataInstance.getStringDataInstance(d).getStringValue();
                if (s == null) continue;
                Set<String> fns = new TreeSet<>();
                for (Reference r : rm.getReferencesTo(d.getAddress())) {
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    fns.add(f == null ? r.getFromAddress().toString() : f.getEntryPoint() + ":" + f.getName());
                }
                sw.printf("%s\t%s\t%s%n", d.getAddress(), s.replace("\t", "\\t").replace("\n", "\\n"), String.join(",", fns));
            }
        }

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out + "/decomp.c")))) {
            int n = 0;
            for (Function f : fm.getFunctions(true)) {
                if (monitor.isCancelled()) break;
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                w.printf("%n//==== %s %s%n", f.getEntryPoint(), f.getName());
                if (r != null && r.decompileCompleted()) w.print(r.getDecompiledFunction().getC());
                else w.println("// decompile failed");
                if (++n % 2000 == 0) println("decompiled " + n);
            }
        }
        di.dispose();
    }
}
