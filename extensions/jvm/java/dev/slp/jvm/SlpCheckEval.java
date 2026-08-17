package dev.slp.jvm;

import sleep.engine.atoms.CheckEval;
import sleep.runtime.Scalar;
import sleep.runtime.ScriptEnvironment;
import sleep.runtime.SleepUtils;

/** A concrete CheckEval leaf evaluated by its corresponding SLP predicate. */
public final class SlpCheckEval extends CheckEval {
    private final Bridge bridge;
    private final long handle;

    public SlpCheckEval(Bridge bridge, long handle, String name) {
        super(name, new SlpBlock(bridge, handle));
        this.bridge = bridge;
        this.handle = handle;
    }

    @Override
    public boolean check(ScriptEnvironment environment) {
        Scalar value = (Scalar) bridge.invokeSlpBlock(handle);
        return SleepUtils.isTrueScalar(value);
    }
}
