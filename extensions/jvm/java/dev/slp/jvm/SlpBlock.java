package dev.slp.jvm;

import sleep.engine.Block;
import sleep.runtime.Scalar;
import sleep.runtime.ScriptEnvironment;

/** A real Sleep Block that enters a retained SLP closure when evaluated. */
public final class SlpBlock extends Block {
    private final Bridge bridge;
    private final long handle;

    public SlpBlock(Bridge bridge, long handle) {
        super("<SLP>");
        this.bridge = bridge;
        this.handle = handle;
    }

    @Override
    public Scalar evaluate(ScriptEnvironment environment) {
        return (Scalar) bridge.invokeSlpBlock(handle);
    }
}
