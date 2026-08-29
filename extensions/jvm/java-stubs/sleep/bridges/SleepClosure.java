package sleep.bridges;

import java.util.Stack;

import sleep.engine.Block;
import sleep.runtime.Scalar;
import sleep.runtime.ScriptInstance;

/** Compile-only signature stub; sleep.jar supplies the runtime class. */
public class SleepClosure implements Runnable {
    public SleepClosure(ScriptInstance owner, Block code) {}
    public Scalar callClosure(String message, ScriptInstance script,
                              Stack locals) { return null; }
    public Scalar evaluate(String message, ScriptInstance script,
                           Stack locals) { return null; }
    public String toStringGeneric() { return ""; }
    public void run() {}
}
