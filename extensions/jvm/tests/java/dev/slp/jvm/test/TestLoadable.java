package dev.slp.jvm.test;

import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Hashtable;
import java.util.List;
import java.util.Stack;

import sleep.bridges.BridgeUtilities;
import sleep.bridges.SleepClosure;
import sleep.engine.Block;
import sleep.engine.atoms.Check;
import sleep.engine.atoms.CheckAnd;
import sleep.engine.atoms.CheckEval;
import sleep.engine.atoms.CheckOr;
import sleep.interfaces.Environment;
import sleep.interfaces.FilterEnvironment;
import sleep.interfaces.Function;
import sleep.interfaces.Loadable;
import sleep.interfaces.Operator;
import sleep.interfaces.Predicate;
import sleep.interfaces.PredicateEnvironment;
import sleep.runtime.Scalar;
import sleep.runtime.ScriptInstance;
import sleep.runtime.SleepUtils;

/** Integration fixture exercising the public Sleep Loadable API. */
public final class TestLoadable implements Loadable, Function, Predicate,
        Operator, Environment, FilterEnvironment, PredicateEnvironment {
    private int loadCalls;
    private Block boundBlock;
    private ScriptInstance boundScript;
    private Block filteredBlock;
    private String filteredParameter;
    private final List<Check> predicateChecks = new ArrayList<Check>();
    private final List<Block> predicateBlocks = new ArrayList<Block>();
    private Scalar retainedArray;
    private Scalar retainedArrayEntry;
    private Scalar retainedHash;
    private Scalar retainedHashEntry;
    private Scalar retainedCycle;
    private Scalar javaCreatedArray;
    private Scalar javaCreatedArrayEntry;
    private Scalar javaCreatedHash;
    private Scalar javaCreatedHashEntry;

    public void scriptLoaded(ScriptInstance script) {
        loadCalls++;
        Hashtable environment =
            script.getScriptEnvironment().getEnvironment();
        environment.put("&javaAdd", this);
        environment.put("&javaOrder", this);
        environment.put("&javaScriptName", this);
        environment.put("&javaObject", this);
        environment.put("&javaArraySize", this);
        environment.put("&javaLong", this);
        environment.put("&javaFlagError", this);
        environment.put("&javaReadVariable", this);
        environment.put("&javaWriteVariables", this);
        environment.put("&javaCallClosure", this);
        environment.put("&javaMakeClosure", this);
        environment.put("&javaRememberCollections", this);
        environment.put("&javaCollectionAliases", this);
        environment.put("&javaRetainedArrayValue", this);
        environment.put("&javaRetainedHashValue", this);
        environment.put("&javaMutateRetainedCollections", this);
        environment.put("&javaCycleIdentity", this);
        environment.put("&javaMutateCycle", this);
        environment.put("&javaWriteAliasedCollections", this);
        environment.put("&javaCreatedArrayValue", this);
        environment.put("&javaCreatedHashValue", this);
        environment.put("&javaMutateCreatedCollections", this);
        environment.put("-javaeven", this);
        environment.put("javaeq", this);
        environment.put("+", this);
        environment.put("javaenv", this);
        environment.put("javafilter", this);
        environment.put("javapredenv", this);
    }

    public void scriptUnloaded(ScriptInstance script) {
        String marker = System.getProperty("slp.test.unload.marker");
        if (marker == null) {
            return;
        }
        try {
            int unloadValue = script.getScriptVariables()
                .getScalar("$unloadValue").intValue();
            FileOutputStream output = new FileOutputStream(marker);
            output.write(((script.isLoaded() ? "loaded" : "unloaded")
                + ":" + loadCalls + ":" + unloadValue)
                .getBytes(StandardCharsets.UTF_8));
            output.close();
        } catch (Exception exception) {
            throw new RuntimeException(exception);
        }
    }

    public Scalar evaluate(String name, ScriptInstance script, Stack args) {
        if (name.equals("&javaAdd")) {
            int left = BridgeUtilities.getInt(args, 0);
            int right = BridgeUtilities.getInt(args, 0);
            return SleepUtils.getScalar(left + right);
        }
        if (name.equals("&javaOrder")) {
            int first = BridgeUtilities.getInt(args, 0);
            int second = BridgeUtilities.getInt(args, 0);
            return SleepUtils.getScalar(first * 10 + second);
        }
        if (name.equals("&javaScriptName")) {
            return SleepUtils.getScalar(script.getName());
        }
        if (name.equals("&javaObject")) {
            return SleepUtils.getScalar(new StringBuilder("loadable-object"));
        }
        if (name.equals("&javaArraySize")) {
            Scalar value = BridgeUtilities.getScalar(args);
            return SleepUtils.getScalar(value.getArray().size());
        }
        if (name.equals("&javaLong")) {
            return SleepUtils.getScalar(9007199254740993L);
        }
        if (name.equals("&javaFlagError")) {
            script.getScriptEnvironment().flagError("loadable-error");
            return SleepUtils.getEmptyScalar();
        }
        if (name.equals("&javaReadVariable")) {
            return SleepUtils.getScalar(
                script.getScriptVariables().getScalar("$fromSlp").intValue());
        }
        if (name.equals("&javaWriteVariables")) {
            script.getScriptVariables().putScalar(
                "$fromJava", SleepUtils.getScalar(91));
            Scalar array = SleepUtils.getArrayScalar();
            array.getArray().push(SleepUtils.getScalar("java"));
            array.getArray().push(SleepUtils.getScalar(2));
            script.getScriptVariables().putScalar("@fromJava", array);
            Scalar hash = SleepUtils.getHashScalar();
            hash.getHash().getAt(SleepUtils.getScalar("answer"))
                .setValue(SleepUtils.getScalar(42));
            script.getScriptVariables().putScalar("%fromJava", hash);
            return SleepUtils.getEmptyScalar();
        }
        if (name.equals("&javaCallClosure")) {
            SleepClosure closure = BridgeUtilities.getFunction(args, script);
            return closure.callClosure("from-java", script, args);
        }
        if (name.equals("&javaMakeClosure")) {
            final ScriptInstance owner = script;
            SleepClosure closure = new SleepClosure(
                    owner, new Block("<fixture>") {}) {
                @Override
                public Scalar callClosure(String message,
                                          ScriptInstance caller,
                                          Stack locals) {
                    int value = BridgeUtilities.getInt(locals, 0);
                    return SleepUtils.getScalar(value * 3);
                }

                @Override
                public Scalar evaluate(String message,
                                       ScriptInstance caller,
                                       Stack locals) {
                    return callClosure(message, caller, locals);
                }
            };
            return SleepUtils.getScalar(closure);
        }
        if (name.equals("&javaRememberCollections")) {
            retainedArray = script.getScriptVariables()
                .getScalar("@liveArray");
            retainedHash = script.getScriptVariables()
                .getScalar("%liveHash");
            retainedArrayEntry = retainedArray.getArray().getAt(0);
            retainedHashEntry = retainedHash.getHash()
                .getAt(SleepUtils.getScalar("answer"));
            retainedCycle = script.getScriptVariables()
                .getScalar("@cycle");
            return SleepUtils.getEmptyScalar();
        }
        if (name.equals("&javaCollectionAliases")) {
            Scalar arrayAlias = script.getScriptVariables()
                .getScalar("@liveAlias");
            Scalar hashAlias = script.getScriptVariables()
                .getScalar("%liveHashAlias");
            return SleepUtils.getScalar(
                retainedArray.getArray() == arrayAlias.getArray() &&
                retainedHash.getHash() == hashAlias.getHash());
        }
        if (name.equals("&javaRetainedArrayValue")) {
            return SleepUtils.getScalar(
                retainedArrayEntry.intValue());
        }
        if (name.equals("&javaRetainedHashValue")) {
            return SleepUtils.getScalar(
                retainedHashEntry.intValue());
        }
        if (name.equals("&javaMutateRetainedCollections")) {
            retainedArrayEntry.setValue(SleepUtils.getScalar(17));
            retainedHashEntry.setValue(SleepUtils.getScalar(18));
            return SleepUtils.getEmptyScalar();
        }
        if (name.equals("&javaCycleIdentity")) {
            Scalar child = retainedCycle.getArray().getAt(0);
            return SleepUtils.getScalar(
                child.getArray() == retainedCycle.getArray());
        }
        if (name.equals("&javaMutateCycle")) {
            retainedCycle.getArray().push(SleepUtils.getScalar(19));
            return SleepUtils.getEmptyScalar();
        }
        if (name.equals("&javaWriteAliasedCollections")) {
            javaCreatedArray = SleepUtils.getArrayScalar();
            javaCreatedArrayEntry = javaCreatedArray.getArray()
                .push(SleepUtils.getScalar(30));
            javaCreatedHash = SleepUtils.getHashScalar();
            javaCreatedHashEntry = javaCreatedHash.getHash()
                .getAt(SleepUtils.getScalar("answer"));
            javaCreatedHashEntry.setValue(SleepUtils.getScalar(40));
            script.getScriptVariables().putScalar(
                "@javaShared", javaCreatedArray);
            script.getScriptVariables().putScalar(
                "@javaSharedAlias", javaCreatedArray);
            script.getScriptVariables().putScalar(
                "%javaShared", javaCreatedHash);
            script.getScriptVariables().putScalar(
                "%javaSharedAlias", javaCreatedHash);
            return SleepUtils.getEmptyScalar();
        }
        if (name.equals("&javaCreatedArrayValue")) {
            return SleepUtils.getScalar(
                javaCreatedArrayEntry.intValue());
        }
        if (name.equals("&javaCreatedHashValue")) {
            return SleepUtils.getScalar(
                javaCreatedHashEntry.intValue());
        }
        if (name.equals("&javaMutateCreatedCollections")) {
            javaCreatedArrayEntry.setValue(SleepUtils.getScalar(50));
            javaCreatedHashEntry.setValue(SleepUtils.getScalar(60));
            return SleepUtils.getEmptyScalar();
        }
        if (name.equals("&javaRunBound")) {
            return SleepUtils.runCode(
                boundBlock, boundScript.getScriptEnvironment());
        }
        if (name.equals("&javaRunFiltered")) {
            Scalar result = SleepUtils.runCode(
                filteredBlock, boundScript.getScriptEnvironment());
            return SleepUtils.getScalar(
                filteredParameter + ":" + result.toString());
        }
        if (name.equals("&javaPredicateMatch")) {
            int index = BridgeUtilities.getInt(args, 0);
            return SleepUtils.getScalar(
                predicateChecks.get(index).check(
                    script.getScriptEnvironment()));
        }
        if (name.equals("&javaRunPredicate")) {
            int index = BridgeUtilities.getInt(args, 0);
            if (!predicateChecks.get(index).check(
                    script.getScriptEnvironment())) {
                return SleepUtils.getEmptyScalar();
            }
            return SleepUtils.runCode(
                predicateBlocks.get(index),
                script.getScriptEnvironment());
        }
        if (name.equals("&javaPredicateDescription")) {
            int index = BridgeUtilities.getInt(args, 0);
            return SleepUtils.getScalar(
                describeCheck(predicateChecks.get(index)));
        }
        return SleepUtils.getEmptyScalar();
    }

    public boolean decide(String name, ScriptInstance script, Stack terms) {
        if (name.equals("-javaeven")) {
            return BridgeUtilities.getInt(terms, 0) % 2 == 0;
        }
        int right = BridgeUtilities.getInt(terms, 0);
        int left = BridgeUtilities.getInt(terms, 0);
        return name.equals("javaeq") && left == right;
    }

    public Scalar operate(String name, ScriptInstance script, Stack terms) {
        int left = BridgeUtilities.getInt(terms, 0);
        int right = BridgeUtilities.getInt(terms, 0);
        return SleepUtils.getScalar(left * 100 + right);
    }

    public void bindFunction(ScriptInstance script, String keyword,
                             String identifier, Block body) {
        boundScript = script;
        boundBlock = body;
        script.getScriptEnvironment().getEnvironment()
            .put("&javaRunBound", this);
    }

    public void bindFilteredFunction(ScriptInstance script, String keyword,
                                     String identifier, String parameter,
                                     Block body) {
        boundScript = script;
        filteredBlock = body;
        filteredParameter = parameter;
        script.getScriptEnvironment().getEnvironment()
            .put("&javaRunFiltered", this);
    }

    public void bindPredicate(ScriptInstance script, String keyword,
                              Check condition, Block body) {
        boundScript = script;
        condition.setInfo(123);
        predicateChecks.add(condition);
        predicateBlocks.add(body);
        Hashtable environment =
            script.getScriptEnvironment().getEnvironment();
        environment.put("&javaPredicateMatch", this);
        environment.put("&javaRunPredicate", this);
        environment.put("&javaPredicateDescription", this);
    }

    private String describeCheck(Check check) {
        if (check instanceof CheckEval) {
            return "Eval:" + ((CheckEval) check).name;
        }
        try {
            Class type = check instanceof CheckAnd
                ? CheckAnd.class : CheckOr.class;
            Field left = type.getDeclaredField("left");
            Field right = type.getDeclaredField("right");
            left.setAccessible(true);
            right.setAccessible(true);
            return (check instanceof CheckAnd ? "And(" : "Or(") +
                describeCheck((Check) left.get(check)) + "," +
                describeCheck((Check) right.get(check)) + ")";
        } catch (Exception exception) {
            throw new RuntimeException(exception);
        }
    }
}
