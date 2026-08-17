package sleep.engine.atoms;

import sleep.engine.Block;
import sleep.runtime.ScriptEnvironment;

/** Compile-only signature stub; sleep.jar supplies the runtime class. */
public class CheckEval implements Check {
    public String name;
    public CheckEval(String name, Block setup) { this.name = name; }
    public boolean check(ScriptEnvironment environment) { return false; }
    public String toString(String prefix) { return ""; }
    public void setInfo(int hint) {}
}
