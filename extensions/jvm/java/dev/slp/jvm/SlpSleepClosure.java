package dev.slp.jvm;

import java.util.Stack;

import sleep.bridges.SleepClosure;
import sleep.runtime.Scalar;
import sleep.runtime.ScriptInstance;

/** A real SleepClosure whose body is retained and executed by the SLP VM. */
public final class SlpSleepClosure extends SleepClosure {
    private final Bridge bridge;
    private final long handle;

    public SlpSleepClosure(Bridge bridge, ScriptInstance owner, long handle) {
        super(owner, new SlpBlock(bridge, handle));
        this.bridge = bridge;
        this.handle = handle;
    }

    @Override
    public Scalar callClosure(String message, ScriptInstance script,
                              Stack locals) {
        return (Scalar) bridge.invokeSlpCallable(
            handle, message == null ? "" : message, locals);
    }

    @Override
    public Scalar evaluate(String message, ScriptInstance script,
                           Stack locals) {
        return callClosure(message, script, locals);
    }

    @Override
    public void run() {
        callClosure("run", null, null);
    }

    @Override
    public String toStringGeneric() {
        return "&closure[<SLP>]";
    }

    public long nativeHandle() {
        return handle;
    }
}
