package sleep.engine.atoms;

import sleep.runtime.ScriptEnvironment;

/** Compile-only signature stub; sleep.jar supplies the runtime interface. */
public interface Check {
    boolean check(ScriptEnvironment environment);
    String toString(String prefix);
    void setInfo(int hint);
}
