package dev.slp.jvm;

import java.io.File;
import java.lang.reflect.Array;
import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.lang.reflect.Proxy;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLClassLoader;
import java.util.ArrayList;
import java.util.Hashtable;
import java.util.HashMap;
import java.util.HashSet;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.Stack;

/** Reflection and class-loader policy kept on the Java side of the JNI seam. */
public final class Bridge {
    private static final Object NO_CONVERSION = new Object();

    private final Map<String, Class<?>> aliases =
        new HashMap<String, Class<?>>();
    private final List<String> packages = new ArrayList<String>();
    private final Map<Class<?>, Object> loadableInstances =
        new HashMap<Class<?>, Object>();
    private final Map<String, Object> loadableFunctions =
        new HashMap<String, Object>();
    private final Map<String, Object> loadablePredicates =
        new HashMap<String, Object>();
    private final Map<String, Object> loadableOperators =
        new HashMap<String, Object>();
    private final Map<String, Object> loadableEnvironments =
        new HashMap<String, Object>();
    private final Map<String, Object> loadableFilters =
        new HashMap<String, Object>();
    private final Map<String, Object> loadablePredicateEnvironments =
        new HashMap<String, Object>();
    private final Set<String> synchronizedVariables =
        new HashSet<String>();
    private final Map<Long, ArrayValue> arrayValues =
        new HashMap<Long, ArrayValue>();
    private final Map<Long, HashValue> hashValues =
        new HashMap<Long, HashValue>();
    private final Map<Long, Object> collectionScalars =
        new HashMap<Long, Object>();
    private final IdentityHashMap<Object, Long> collectionIdentities =
        new IdentityHashMap<Object, Long>();
    private final Set<Long> syncingCollections =
        new HashSet<Long>();
    private final List<Object> activeLoadables = new ArrayList<Object>();
    private ClassLoader loader;
    private Object scriptInstance;
    private Class<?> scriptInstanceClass;
    private Class<?> scalarClass;
    private Class<?> functionClass;
    private Class<?> predicateClass;
    private Class<?> operatorClass;
    private Class<?> environmentClass;
    private Class<?> filterEnvironmentClass;
    private Class<?> predicateEnvironmentClass;
    private Class<?> checkClass;
    private Class<?> checkEvalClass;
    private Class<?> checkAndClass;
    private Class<?> checkOrClass;
    private Class<?> blockClass;
    private Class<?> sleepClosureClass;
    private Class<?> sleepUtilsClass;
    private Object lastFunctionError;
    private long nativeState;
    private long nextJavaCollectionIdentity = -1L;

    public Bridge() {
        loader = Thread.currentThread().getContextClassLoader();
        if (loader == null) {
            loader = Bridge.class.getClassLoader();
        }
        packages.add("java.lang");
    }

    public void setNativeState(long state) {
        nativeState = state;
    }

    private native Object invokeSlpCallableNative(
        long state, long handle, String message, Object[] arguments);

    public void importTarget(String target, String sourcePath)
            throws Exception {
        if (sourcePath != null && !sourcePath.isEmpty()) {
            URL url = toUrl(sourcePath);
            loader = new URLClassLoader(new URL[] {url}, loader);
        }

        if (target.endsWith(".*")) {
            String packageName = target.substring(0, target.length() - 2);
            if (!packages.contains(packageName)) {
                packages.add(packageName);
            }
            return;
        }

        Class<?> type = load(target);
        int separator = target.lastIndexOf('.');
        String simpleName = separator < 0
            ? target
            : target.substring(separator + 1);
        aliases.put(simpleName, type);
        aliases.put(target, type);
    }

    public Class<?> resolve(String name) {
        Class<?> known = aliases.get(name);
        if (known != null) {
            return known;
        }

        if (name.indexOf('.') >= 0) {
            return tryLoad(name);
        }

        for (int index = packages.size() - 1; index >= 0; index--) {
            Class<?> type = tryLoad(packages.get(index) + "." + name);
            if (type != null) {
                aliases.put(name, type);
                return type;
            }
        }
        return null;
    }

    public Object construct(String name, Object[] arguments) throws Exception {
        Class<?> type = resolve(name);
        if (type == null) {
            throw new ClassNotFoundException(name);
        }
        Match<Constructor<?>> match = best(type.getConstructors(), arguments,
                                           false);
        if (match == null) {
            throw new NoSuchMethodException(
                type.getName() + " constructor with " + arguments.length
                    + " argument(s)");
        }
        try {
            return match.executable.newInstance(match.arguments);
        } catch (InvocationTargetException exception) {
            throwTarget(exception);
            return null;
        }
    }

    public Object invoke(Object target, String name, Object[] arguments)
            throws Exception {
        boolean staticCall = target instanceof Class<?>;
        Class<?> type = staticCall ? (Class<?>) target : target.getClass();
        Match<Method> match = best(type.getMethods(), arguments, staticCall,
                                   name);
        if (match != null) {
            try {
                return match.executable.invoke(
                    staticCall ? null : target, match.arguments);
            } catch (InvocationTargetException exception) {
                throwTarget(exception);
                return null;
            }
        }

        if (arguments.length == 0) {
            Field field = type.getField(name);
            if (!staticCall || Modifier.isStatic(field.getModifiers())) {
                return field.get(staticCall ? null : target);
            }
        }
        throw new NoSuchMethodException(
            type.getName() + "." + name + " with " + arguments.length
                + " argument(s)");
    }

    /** Load a standard Sleep Loadable and return typed registrations. */
    public String[] loadLoadable(String sourcePath, String className,
                                 String scriptName, String[] variableNames,
                                 Object[] variableValues) throws Exception {
        if (sourcePath != null && !sourcePath.isEmpty()) {
            importTarget(className, sourcePath);
        }
        ensureSleepRuntime(scriptName);
        syncVariables(variableNames, variableValues);

        Class<?> type = resolve(className);
        if (type == null) {
            type = load(className);
        }
        Class<?> loadableClass = load("sleep.interfaces.Loadable");
        if (!loadableClass.isAssignableFrom(type)) {
            throw new IllegalArgumentException(
                type.getName() + " does not implement sleep.interfaces.Loadable");
        }

        Object loadable = loadableInstances.get(type);
        if (loadable == null) {
            loadable = type.getDeclaredConstructor().newInstance();
            loadableInstances.put(type, loadable);
        }
        call(loadableClass.getMethod(
                 "scriptLoaded", scriptInstanceClass),
             loadable, scriptInstance);
        if (!isActiveLoadable(loadable)) {
            activeLoadables.add(loadable);
        }

        return discoverRegistrations();
    }

    private String[] discoverRegistrations() throws Exception {
        Hashtable<?, ?> environment = scriptEnvironment();
        List<String> registrations = new ArrayList<String>();
        for (Map.Entry<?, ?> entry : environment.entrySet()) {
            if (!(entry.getKey() instanceof String)) {
                continue;
            }
            String name = (String) entry.getKey();
            Object value = entry.getValue();
            if (name.startsWith("&") && functionClass.isInstance(value)) {
                loadableFunctions.put(name, value);
                registrations.add("F:" + name);
            }
            if (predicateClass.isInstance(value)) {
                loadablePredicates.put(name, value);
                registrations.add("P:" + name);
            }
            if (operatorClass.isInstance(value)) {
                loadableOperators.put(name, value);
                registrations.add("O:" + name);
            }
            if (environmentClass.isInstance(value)) {
                loadableEnvironments.put(name, value);
                registrations.add("E:" + name);
            }
            if (filterEnvironmentClass != null &&
                    filterEnvironmentClass.isInstance(value)) {
                loadableFilters.put(name, value);
                registrations.add("R:" + name);
            }
            if (predicateEnvironmentClass != null &&
                    predicateEnvironmentClass.isInstance(value)) {
                loadablePredicateEnvironments.put(name, value);
                registrations.add("Q:" + name);
            }
        }
        return registrations.toArray(new String[registrations.size()]);
    }

    public void prepareSleepRuntime(String scriptName) throws Exception {
        ensureSleepRuntime(scriptName);
    }

    private boolean isActiveLoadable(Object loadable) {
        for (Object active : activeLoadables) {
            if (active == loadable) {
                return true;
            }
        }
        return false;
    }

    /** Invoke a Function registered by a Loadable using real Sleep Scalars. */
    public Object invokeFunction(String name, Object[] arguments)
            throws Exception {
        lastFunctionError = null;
        String sleepName = name.startsWith("&") ? name : "&" + name;
        Object function = loadableFunctions.get(sleepName);
        if (function == null) {
            throw new NoSuchMethodException(
                "Sleep Loadable function is no longer registered: " + sleepName);
        }

        Stack<Object> locals = new Stack<Object>();
        for (int index = arguments.length - 1; index >= 0; index--) {
            locals.push(toScalar(arguments[index]));
        }
        LoadableFunctionInvocation invocation =
            new LoadableFunctionInvocation(function);
        Object wrappedFunction = Proxy.newProxyInstance(
            loader, new Class<?>[] {functionClass}, invocation);
        Method runCode = sleepUtilsClass.getMethod(
            "runCode", functionClass, String.class,
            scriptInstanceClass, Stack.class);
        Object result = call(
            runCode, null, wrappedFunction, sleepName,
            scriptInstance, locals);
        if (!isEmptyScalar(invocation.error)) {
            lastFunctionError = fromScalar(invocation.error);
        }
        return fromScalar(result);
    }

    public Boolean decide(String name, Object[] terms) throws Exception {
        Object predicate = loadablePredicates.get(name);
        if (predicate == null) {
            return null;
        }
        Stack<Object> values = new Stack<Object>();
        /* Predicate terms are documented and consumed right-hand side first. */
        for (int index = 0; index < terms.length; index++) {
            values.push(toScalar(terms[index]));
        }
        Object result = call(
            predicateClass.getMethod(
                "decide", String.class, scriptInstanceClass, Stack.class),
            predicate, name, scriptInstance, values);
        return (Boolean) result;
    }

    public boolean hasPredicate(String name) {
        return loadablePredicates.containsKey(name);
    }

    public Object operate(String name, Object[] terms) throws Exception {
        Object operator = loadableOperators.get(name);
        if (operator == null) {
            return null;
        }
        Stack<Object> values = new Stack<Object>();
        /* Operator implementations pop the left term before the right term. */
        for (int index = terms.length - 1; index >= 0; index--) {
            values.push(toScalar(terms[index]));
        }
        Object result = call(
            operatorClass.getMethod(
                "operate", String.class, scriptInstanceClass, Stack.class),
            operator, name, scriptInstance, values);
        return fromScalar(result);
    }

    public boolean hasOperator(String name) {
        return loadableOperators.containsKey(name);
    }

    public String[] bindEnvironment(String keyword, String identifier,
                                    String stringArgument,
                                    long[] conditionHandles, long handle)
            throws Exception {
        Object block = load("dev.slp.jvm.SlpBlock")
            .getConstructor(Bridge.class, long.class)
            .newInstance(this, Long.valueOf(handle));
        Object predicateEnvironment =
            loadablePredicateEnvironments.get(keyword);
        if (conditionHandles != null && conditionHandles.length > 0 &&
                predicateEnvironment != null) {
            Object condition = new CheckTreeParser(
                stringArgument, conditionHandles).parse();
            call(predicateEnvironmentClass.getMethod(
                    "bindPredicate", scriptInstanceClass,
                    String.class, checkClass, blockClass),
                predicateEnvironment, scriptInstance, keyword,
                condition, block);
            return discoverRegistrations();
        }
        Object filter = loadableFilters.get(keyword);
        if (stringArgument != null && filter != null) {
            call(filterEnvironmentClass.getMethod(
                    "bindFilteredFunction", scriptInstanceClass,
                    String.class, String.class, String.class, blockClass),
                filter, scriptInstance, keyword, identifier,
                stringArgument, block);
            return discoverRegistrations();
        }
        Object environment = loadableEnvironments.get(keyword);
        if (environment != null) {
            call(environmentClass.getMethod(
                    "bindFunction", scriptInstanceClass, String.class,
                    String.class, blockClass),
                environment, scriptInstance, keyword, identifier, block);
            return discoverRegistrations();
        }
        if (filter != null) {
            call(filterEnvironmentClass.getMethod(
                    "bindFilteredFunction", scriptInstanceClass,
                    String.class, String.class, String.class, blockClass),
                filter, scriptInstance, keyword, identifier,
                stringArgument, block);
            return discoverRegistrations();
        }
        throw new IllegalStateException(
            "Sleep environment is no longer registered: " + keyword);
    }

    public Object wrapSlpClosure(long handle) throws Exception {
        return load("dev.slp.jvm.SlpSleepClosure")
            .getConstructor(Bridge.class, scriptInstanceClass, long.class)
            .newInstance(this, scriptInstance, Long.valueOf(handle));
    }

    public long slpClosureHandle(Object value) throws Exception {
        if (value == null || !value.getClass().getName().equals(
                "dev.slp.jvm.SlpSleepClosure")) {
            return 0L;
        }
        return ((Long) call(
            value.getClass().getMethod("nativeHandle"), value)).longValue();
    }

    public boolean isSleepClosure(Object value) {
        return sleepClosureClass != null &&
            sleepClosureClass.isInstance(value);
    }

    public Object invokeSleepClosure(Object closure, String message,
                                     Object[] arguments) throws Exception {
        if (!isSleepClosure(closure)) {
            throw new IllegalArgumentException("object is not a SleepClosure");
        }
        Stack<Object> locals = new Stack<Object>();
        for (int index = arguments.length - 1; index >= 0; index--) {
            locals.push(toScalar(arguments[index]));
        }
        Object result = call(
            sleepClosureClass.getMethod(
                "callClosure", String.class, scriptInstanceClass,
                Stack.class),
            closure, message, scriptInstance, locals);
        return fromScalar(result);
    }

    public Object invokeSlpCallable(long handle, String message, Stack locals) {
        try {
            Object[] arguments;
            if (locals == null) {
                arguments = new Object[0];
            } else {
                arguments = new Object[locals.size()];
                for (int index = 0; index < arguments.length; index++) {
                    arguments[index] = fromScalar(
                        locals.get(arguments.length - index - 1));
                }
            }
            Object result = invokeSlpCallableNative(
                nativeState, handle, message, arguments);
            return toScalar(result);
        } catch (Exception exception) {
            throw new RuntimeException(exception);
        }
    }

    public Object invokeSlpBlock(long handle) {
        return invokeSlpCallable(handle, "", null);
    }

    public void syncVariables(String[] names, Object[] values)
            throws Exception {
        if (scriptInstance == null) {
            return;
        }
        Object variables = call(
            scriptInstanceClass.getMethod("getScriptVariables"),
            scriptInstance);
        Method put = variables.getClass().getMethod(
            "putScalar", String.class, scalarClass);
        Object global = call(
            variables.getClass().getMethod("getGlobalVariables"), variables);
        Method remove = global.getClass().getMethod(
            "removeScalar", String.class);
        Method exists = global.getClass().getMethod(
            "scalarExists", String.class);
        Method get = global.getClass().getMethod(
            "getScalar", String.class);
        Set<String> incoming = new HashSet<String>();
        for (int index = 0; index < names.length; index++) {
            incoming.add(names[index]);
            Long identity = collectionIdentity(values[index]);
            if (identity != null &&
                    !collectionScalars.containsKey(identity) &&
                    ((Boolean) call(exists, global, names[index]))
                        .booleanValue()) {
                adoptCollectionScalar(
                    values[index], identity,
                    call(get, global, names[index]));
            }
            call(put, variables, names[index], toScalar(values[index]));
        }
        for (String old : new ArrayList<String>(synchronizedVariables)) {
            if (!incoming.contains(old)) {
                call(remove, global, old);
            }
        }
        synchronizedVariables.clear();
        synchronizedVariables.addAll(incoming);
    }

    public Object[] snapshotVariables() throws Exception {
        if (scriptInstance == null) {
            return new Object[0];
        }
        Object variables = call(
            scriptInstanceClass.getMethod("getScriptVariables"),
            scriptInstance);
        Object global = call(
            variables.getClass().getMethod("getGlobalVariables"), variables);
        Map<?, ?> values = variableMap(global);
        List<Object> snapshot = new ArrayList<Object>();
        if (values != null) {
            for (Map.Entry<?, ?> entry : values.entrySet()) {
                if (!(entry.getKey() instanceof String)) {
                    continue;
                }
                String name = (String) entry.getKey();
                if (!isVariableName(name)) {
                    continue;
                }
                snapshot.add(name);
                snapshot.add(fromScalar(entry.getValue()));
            }
        } else {
            Method exists = global.getClass().getMethod(
                "scalarExists", String.class);
            Method get = global.getClass().getMethod(
                "getScalar", String.class);
            for (String name : synchronizedVariables) {
                if (((Boolean) call(exists, global, name)).booleanValue()) {
                    snapshot.add(name);
                    snapshot.add(fromScalar(call(get, global, name)));
                }
            }
        }
        return snapshot.toArray(new Object[snapshot.size()]);
    }

    private Map<?, ?> variableMap(Object variables) throws Exception {
        Class<?> type = variables.getClass();
        while (type != null) {
            try {
                Field field = type.getDeclaredField("values");
                field.setAccessible(true);
                Object value = field.get(variables);
                return value instanceof Map<?, ?> ? (Map<?, ?>) value : null;
            } catch (NoSuchFieldException exception) {
                type = type.getSuperclass();
            }
        }
        return null;
    }

    private static boolean isVariableName(String name) {
        return name != null && !name.isEmpty() &&
            (name.charAt(0) == '$' || name.charAt(0) == '@' ||
             name.charAt(0) == '%');
    }

    public ArrayValue makeArray(long identity, Object[] elements) {
        Long key = Long.valueOf(identity);
        ArrayValue value = arrayValues.get(key);
        if (value == null) {
            value = new ArrayValue(identity, null);
            arrayValues.put(key, value);
        }
        if (elements != null) {
            value.elements = elements;
        }
        return value;
    }

    public HashValue makeHash(long identity, Object[] entries) {
        Long key = Long.valueOf(identity);
        HashValue value = hashValues.get(key);
        if (value == null) {
            value = new HashValue(identity, null);
            hashValues.put(key, value);
        }
        if (entries != null) {
            value.entries = entries;
        }
        return value;
    }

    public Object takeFunctionError() {
        Object error = lastFunctionError;
        lastFunctionError = null;
        return error;
    }

    /** Mirror ScriptLoader's ordering: mark stale, then notify bridges. */
    public void unloadLoadables() throws Exception {
        if (scriptInstance == null) {
            return;
        }
        call(scriptInstanceClass.getMethod("setUnloaded"), scriptInstance);
        Exception first = null;
        Class<?> loadableClass = load("sleep.interfaces.Loadable");
        Method unloaded = loadableClass.getMethod(
            "scriptUnloaded", scriptInstanceClass);
        for (int index = activeLoadables.size() - 1; index >= 0; index--) {
            try {
                call(unloaded, activeLoadables.get(index), scriptInstance);
            } catch (Exception exception) {
                if (first == null) {
                    first = exception;
                }
            }
        }
        activeLoadables.clear();
        loadableFunctions.clear();
        loadablePredicates.clear();
        loadableOperators.clear();
        loadableEnvironments.clear();
        loadableFilters.clear();
        loadablePredicateEnvironments.clear();
        loadableInstances.clear();
        synchronizedVariables.clear();
        arrayValues.clear();
        hashValues.clear();
        collectionScalars.clear();
        collectionIdentities.clear();
        syncingCollections.clear();
        scriptInstance = null;
        if (first != null) {
            throw first;
        }
    }

    private void ensureSleepRuntime(String scriptName) throws Exception {
        if (scriptInstance == null) {
            try {
                scriptInstanceClass = load("sleep.runtime.ScriptInstance");
                scalarClass = load("sleep.runtime.Scalar");
                functionClass = load("sleep.interfaces.Function");
                predicateClass = load("sleep.interfaces.Predicate");
                operatorClass = load("sleep.interfaces.Operator");
                environmentClass = load("sleep.interfaces.Environment");
                filterEnvironmentClass =
                    tryLoad("sleep.interfaces.FilterEnvironment");
                predicateEnvironmentClass =
                    tryLoad("sleep.interfaces.PredicateEnvironment");
                checkClass =
                    tryLoad("sleep.engine.atoms.Check");
                checkEvalClass =
                    tryLoad("sleep.engine.atoms.CheckEval");
                checkAndClass =
                    tryLoad("sleep.engine.atoms.CheckAnd");
                checkOrClass =
                    tryLoad("sleep.engine.atoms.CheckOr");
                blockClass = load("sleep.engine.Block");
                sleepClosureClass = load("sleep.bridges.SleepClosure");
                sleepUtilsClass = load("sleep.runtime.SleepUtils");
            } catch (ClassNotFoundException exception) {
                throw new ClassNotFoundException(
                    "Sleep Loadable support requires sleep.jar on the JVM class path",
                    exception);
            }
            scriptInstance = scriptInstanceClass
                .getDeclaredConstructor().newInstance();
        }
        call(scriptInstanceClass.getMethod("setName", String.class),
             scriptInstance,
             scriptName == null ? "<SLP script>" : scriptName);
    }

    private Hashtable<?, ?> scriptEnvironment() throws Exception {
        Object environment = call(
            scriptInstanceClass.getMethod("getScriptEnvironment"),
            scriptInstance);
        return (Hashtable<?, ?>) call(
            environment.getClass().getMethod("getEnvironment"),
            environment);
    }

    private Long collectionIdentity(Object value) {
        if (value instanceof ArrayValue) {
            return Long.valueOf(((ArrayValue) value).identity);
        }
        if (value instanceof HashValue) {
            return Long.valueOf(((HashValue) value).identity);
        }
        return null;
    }

    private void adoptCollectionScalar(
            Object value, Long identity, Object scalar) throws Exception {
        Object container;
        if (value instanceof ArrayValue) {
            container = call(scalarClass.getMethod("getArray"), scalar);
        } else if (value instanceof HashValue) {
            container = call(scalarClass.getMethod("getHash"), scalar);
        } else {
            return;
        }
        if (container != null) {
            collectionScalars.put(identity, scalar);
            collectionIdentities.put(container, identity);
        }
    }

    private Object arrayScalar(ArrayValue value) throws Exception {
        Long identity = Long.valueOf(value.identity);
        Object scalar = collectionScalars.get(identity);
        if (scalar == null) {
            scalar = call(
                sleepUtilsClass.getMethod("getArrayScalar"), null);
            Object array = call(
                scalarClass.getMethod("getArray"), scalar);
            collectionScalars.put(identity, scalar);
            collectionIdentities.put(array, identity);
        }
        if (!syncingCollections.add(identity)) {
            return scalar;
        }
        try {
            Object array = call(
                scalarClass.getMethod("getArray"), scalar);
            collectionIdentities.put(array, identity);
            Method size = array.getClass().getMethod("size");
            Method getAt = array.getClass().getMethod("getAt", int.class);
            Method remove = array.getClass().getMethod("remove", int.class);
            Method push = array.getClass().getMethod("push", scalarClass);
            Method setValue = scalarClass.getMethod(
                "setValue", scalarClass);
            Object[] elements = value.elements == null
                ? new Object[0] : value.elements;
            int current = ((Integer) call(size, array)).intValue();
            while (current > elements.length) {
                call(remove, array, Integer.valueOf(--current));
            }
            for (int index = 0; index < elements.length; index++) {
                if (index < current) {
                    Object destination = call(
                        getAt, array, Integer.valueOf(index));
                    Long childIdentity = collectionIdentity(elements[index]);
                    if (childIdentity != null &&
                            !collectionScalars.containsKey(childIdentity)) {
                        adoptCollectionScalar(
                            elements[index], childIdentity, destination);
                    }
                    Object incoming = toScalar(elements[index]);
                    if (destination != incoming) {
                        call(setValue, destination, incoming);
                    }
                } else {
                    call(push, array, toScalar(elements[index]));
                }
            }
            return scalar;
        } finally {
            syncingCollections.remove(identity);
        }
    }

    private Object hashScalar(HashValue value) throws Exception {
        Long identity = Long.valueOf(value.identity);
        Object scalar = collectionScalars.get(identity);
        if (scalar == null) {
            scalar = call(
                sleepUtilsClass.getMethod("getHashScalar"), null);
            Object hash = call(
                scalarClass.getMethod("getHash"), scalar);
            collectionScalars.put(identity, scalar);
            collectionIdentities.put(hash, identity);
        }
        if (!syncingCollections.add(identity)) {
            return scalar;
        }
        try {
            Object hash = call(
                scalarClass.getMethod("getHash"), scalar);
            collectionIdentities.put(hash, identity);
            Class<?> scalarHashClass = load("sleep.runtime.ScalarHash");
            Class<?> scalarArrayClass = load("sleep.runtime.ScalarArray");
            Method keysMethod = scalarHashClass.getMethod("keys");
            Method hashAt = scalarHashClass.getMethod(
                "getAt", scalarClass);
            Method remove = scalarHashClass.getMethod(
                "remove", scalarClass);
            Method keyAt = scalarArrayClass.getMethod("getAt", int.class);
            Method size = scalarArrayClass.getMethod("size");
            Object[] entries = value.entries == null
                ? new Object[0] : value.entries;
            Map<String, Object> incomingKeys =
                new HashMap<String, Object>();
            for (int index = 0; index + 1 < entries.length; index += 2) {
                Object key = toScalar(entries[index]);
                incomingKeys.put(key.toString(), key);
            }
            Object keys = call(keysMethod, hash);
            int keyCount = ((Integer) call(size, keys)).intValue();
            for (int index = keyCount - 1; index >= 0; index--) {
                Object key = call(keyAt, keys, Integer.valueOf(index));
                if (!incomingKeys.containsKey(key.toString())) {
                    call(remove, hash, key);
                }
            }

            Method setValue = scalarClass.getMethod(
                "setValue", scalarClass);
            for (int index = 0; index + 1 < entries.length; index += 2) {
                Object destination = call(
                    hashAt, hash, toScalar(entries[index]));
                Long childIdentity =
                    collectionIdentity(entries[index + 1]);
                if (childIdentity != null &&
                        !collectionScalars.containsKey(childIdentity)) {
                    adoptCollectionScalar(
                        entries[index + 1], childIdentity, destination);
                }
                Object incoming = toScalar(entries[index + 1]);
                if (destination != incoming) {
                    call(setValue, destination, incoming);
                }
            }
            return scalar;
        } finally {
            syncingCollections.remove(identity);
        }
    }

    private Object toScalar(Object value) throws Exception {
        if (value == null) {
            return call(
                sleepUtilsClass.getMethod("getEmptyScalar"), null);
        }
        if (value instanceof Boolean) {
            return call(
                sleepUtilsClass.getMethod("getScalar", boolean.class),
                null, ((Boolean) value).booleanValue());
        }
        if (value instanceof Byte || value instanceof Short
                || value instanceof Integer) {
            return call(
                sleepUtilsClass.getMethod("getScalar", int.class),
                null, ((Number) value).intValue());
        }
        if (value instanceof Long) {
            return call(
                sleepUtilsClass.getMethod("getScalar", long.class),
                null, ((Long) value).longValue());
        }
        if (value instanceof Float || value instanceof Double) {
            return call(
                sleepUtilsClass.getMethod("getScalar", double.class),
                null, ((Number) value).doubleValue());
        }
        if (value instanceof String) {
            return call(
                sleepUtilsClass.getMethod("getScalar", String.class),
                null, value);
        }
        if (value instanceof ArrayValue) {
            return arrayScalar((ArrayValue) value);
        }
        if (value instanceof HashValue) {
            return hashScalar((HashValue) value);
        }
        if (value instanceof Object[]) {
            Object scalar = call(
                sleepUtilsClass.getMethod("getArrayScalar"), null);
            Object array = call(
                scalarClass.getMethod("getArray"), scalar);
            Method push = array.getClass().getMethod("push", scalarClass);
            for (Object element : (Object[]) value) {
                call(push, array, toScalar(element));
            }
            return scalar;
        }
        return call(
            sleepUtilsClass.getMethod("getScalar", Object.class),
            null, value);
    }

    private Long identityForCollection(
            Object container, Object scalar) {
        Long identity = collectionIdentities.get(container);
        if (identity == null) {
            identity = Long.valueOf(nextJavaCollectionIdentity--);
            collectionIdentities.put(container, identity);
        }
        if (!collectionScalars.containsKey(identity)) {
            collectionScalars.put(identity, scalar);
        }
        return identity;
    }

    private Object fromScalar(Object scalar) throws Exception {
        if (isEmptyScalar(scalar)) {
            return null;
        }
        Object array = call(
            scalarClass.getMethod("getArray"), scalar);
        if (array != null) {
            Long identity = identityForCollection(array, scalar);
            ArrayValue carrier = arrayValues.get(identity);
            if (carrier == null) {
                carrier = new ArrayValue(identity.longValue(), null);
                arrayValues.put(identity, carrier);
            }
            if (!syncingCollections.add(identity)) {
                return carrier;
            }
            Method size = array.getClass().getMethod("size");
            Method getAt = array.getClass().getMethod("getAt", int.class);
            int count = ((Integer) call(size, array)).intValue();
            Object[] values = new Object[count];
            try {
                for (int index = 0; index < count; index++) {
                    values[index] = fromScalar(call(getAt, array, index));
                }
                carrier.elements = values;
                return carrier;
            } finally {
                syncingCollections.remove(identity);
            }
        }
        Object hash = call(scalarClass.getMethod("getHash"), scalar);
        if (hash != null) {
            Long identity = identityForCollection(hash, scalar);
            HashValue carrier = hashValues.get(identity);
            if (carrier == null) {
                carrier = new HashValue(identity.longValue(), null);
                hashValues.put(identity, carrier);
            }
            if (!syncingCollections.add(identity)) {
                return carrier;
            }
            Class<?> scalarHashClass = load("sleep.runtime.ScalarHash");
            Class<?> scalarArrayClass = load("sleep.runtime.ScalarArray");
            Object keys = call(
                scalarHashClass.getMethod("keys"), hash);
            int count = ((Integer) call(
                scalarArrayClass.getMethod("size"), keys)).intValue();
            Method keyAt = scalarArrayClass.getMethod("getAt", int.class);
            Method hashAt = scalarHashClass.getMethod("getAt", scalarClass);
            Object[] entries = new Object[count * 2];
            try {
                for (int index = 0; index < count; index++) {
                    Object key = call(keyAt, keys, Integer.valueOf(index));
                    entries[index * 2] = fromScalar(key);
                    entries[index * 2 + 1] = fromScalar(
                        call(hashAt, hash, key));
                }
                carrier.entries = entries;
                return carrier;
            } finally {
                syncingCollections.remove(identity);
            }
        }

        Object actual = call(
            scalarClass.getMethod("getActualValue"), scalar);
        String type = actual.getClass().getName();
        if (type.equals("sleep.engine.types.IntValue")) {
            return call(scalarClass.getMethod("intValue"), scalar);
        }
        if (type.equals("sleep.engine.types.LongValue")) {
            return call(scalarClass.getMethod("longValue"), scalar);
        }
        if (type.equals("sleep.engine.types.DoubleValue")) {
            return call(scalarClass.getMethod("doubleValue"), scalar);
        }
        if (type.equals("sleep.engine.types.StringValue")) {
            return scalar.toString();
        }
        return call(scalarClass.getMethod("objectValue"), scalar);
    }

    private boolean isEmptyScalar(Object scalar) throws Exception {
        return ((Boolean) call(
            sleepUtilsClass.getMethod("isEmptyScalar", scalarClass),
            null, scalar)).booleanValue();
    }

    private Object takeScriptError() throws Exception {
        Object environment = call(
            scriptInstanceClass.getMethod("getScriptEnvironment"),
            scriptInstance);
        return call(
            environment.getClass().getMethod("checkError"),
            environment);
    }

    private static Object call(Method method, Object target,
                               Object... arguments) throws Exception {
        try {
            return method.invoke(target, arguments);
        } catch (InvocationTargetException exception) {
            throwTarget(exception);
            return null;
        }
    }

    private final class LoadableFunctionInvocation
            implements InvocationHandler {
        private final Object function;
        private Object error;

        LoadableFunctionInvocation(Object function) {
            this.function = function;
        }

        public Object invoke(Object proxy, Method method, Object[] arguments)
                throws Throwable {
            try {
                Object result = method.invoke(function, arguments);
                if (method.getName().equals("evaluate")) {
                    error = takeScriptError();
                }
                return result;
            } catch (InvocationTargetException exception) {
                throw exception.getTargetException();
            }
        }
    }

    private Class<?> load(String name) throws ClassNotFoundException {
        return Class.forName(name, true, loader);
    }

    private Class<?> tryLoad(String name) {
        try {
            return load(name);
        } catch (ClassNotFoundException exception) {
            return null;
        }
    }

    private static URL toUrl(String path) throws MalformedURLException {
        if (path.indexOf("://") >= 0) {
            return new URL(path);
        }
        return new File(path).getAbsoluteFile().toURI().toURL();
    }

    private static <T extends Executable> Match<T> best(
            T[] candidates, Object[] arguments, boolean staticCall) {
        return best(candidates, arguments, staticCall, null);
    }

    private static <T extends Executable> Match<T> best(
            T[] candidates, Object[] arguments, boolean staticCall,
            String methodName) {
        Match<T> best = null;
        for (T candidate : candidates) {
            if (methodName != null) {
                Method method = (Method) candidate;
                if (!method.getName().equals(methodName)
                        || Modifier.isStatic(method.getModifiers())
                            != staticCall) {
                    continue;
                }
            }
            Match<T> converted = convert(candidate, arguments);
            if (converted != null
                    && (best == null || converted.score < best.score)) {
                best = converted;
            }
        }
        return best;
    }

    private static <T extends Executable> Match<T> convert(
            T executable, Object[] arguments) {
        Class<?>[] parameters = executable.getParameterTypes();
        boolean varargs = executable.isVarArgs();
        if ((!varargs && parameters.length != arguments.length)
                || (varargs && arguments.length < parameters.length - 1)) {
            return null;
        }

        Object[] converted = new Object[parameters.length];
        int score = 0;
        int fixedCount = varargs ? parameters.length - 1 : parameters.length;
        for (int index = 0; index < fixedCount; index++) {
            Conversion conversion = convertOne(arguments[index],
                                               parameters[index]);
            if (conversion.value == NO_CONVERSION) {
                return null;
            }
            converted[index] = conversion.value;
            score += conversion.score;
        }

        if (varargs) {
            Class<?> component = parameters[parameters.length - 1]
                                      .getComponentType();
            int count = arguments.length - fixedCount;
            Object array = Array.newInstance(component, count);
            for (int index = 0; index < count; index++) {
                Conversion conversion = convertOne(
                    arguments[fixedCount + index], component);
                if (conversion.value == NO_CONVERSION) {
                    return null;
                }
                Array.set(array, index, conversion.value);
                score += conversion.score + 1;
            }
            converted[converted.length - 1] = array;
        }
        return new Match<T>(executable, converted, score);
    }

    private static Conversion convertOne(Object value, Class<?> target) {
        if (value == null) {
            return target.isPrimitive()
                ? Conversion.failure()
                : new Conversion(null, 1);
        }
        if (target.isPrimitive()) {
            target = wrapper(target);
        }
        if (target.isInstance(value)) {
            return new Conversion(value, 0);
        }
        if (Number.class.isAssignableFrom(target) && value instanceof Number) {
            Number number = (Number) value;
            if (target == Byte.class) return new Conversion(number.byteValue(), 1);
            if (target == Short.class) return new Conversion(number.shortValue(), 1);
            if (target == Integer.class) return new Conversion(number.intValue(), 1);
            if (target == Long.class) return new Conversion(number.longValue(), 1);
            if (target == Float.class) return new Conversion(number.floatValue(), 1);
            if (target == Double.class) return new Conversion(number.doubleValue(), 1);
        }
        if (target == Character.class && value instanceof String
                && ((String) value).length() == 1) {
            return new Conversion(((String) value).charAt(0), 1);
        }
        if (target.isEnum() && value instanceof String) {
            try {
                @SuppressWarnings({"rawtypes", "unchecked"})
                Object enumValue = Enum.valueOf((Class) target, (String) value);
                return new Conversion(enumValue, 2);
            } catch (IllegalArgumentException exception) {
                return Conversion.failure();
            }
        }
        return Conversion.failure();
    }

    private static Class<?> wrapper(Class<?> primitive) {
        if (primitive == boolean.class) return Boolean.class;
        if (primitive == byte.class) return Byte.class;
        if (primitive == short.class) return Short.class;
        if (primitive == int.class) return Integer.class;
        if (primitive == long.class) return Long.class;
        if (primitive == float.class) return Float.class;
        if (primitive == double.class) return Double.class;
        if (primitive == char.class) return Character.class;
        return primitive;
    }

    private static void throwTarget(InvocationTargetException exception)
            throws Exception {
        Throwable target = exception.getTargetException();
        if (target instanceof Exception) {
            throw (Exception) target;
        }
        if (target instanceof Error) {
            throw (Error) target;
        }
        throw new RuntimeException(target);
    }

    private final class CheckTreeParser {
        private final String shape;
        private final long[] handles;
        private int position;
        private int handleIndex;

        CheckTreeParser(String shape, long[] handles) {
            this.shape = shape == null ? "" : shape;
            this.handles = handles;
        }

        Object parse() throws Exception {
            Object result = parseCheck();
            if (position != shape.length() ||
                    handleIndex != handles.length) {
                throw new IllegalArgumentException(
                    "invalid SLP predicate Check descriptor");
            }
            return result;
        }

        private Object parseCheck() throws Exception {
            if (position >= shape.length()) {
                throw new IllegalArgumentException(
                    "truncated SLP predicate Check descriptor");
            }
            char kind = shape.charAt(position++);
            if (kind == 'P') {
                int length = 0;
                boolean sawDigit = false;
                while (position < shape.length() &&
                        Character.isDigit(shape.charAt(position))) {
                    sawDigit = true;
                    length = length * 10 +
                        (shape.charAt(position++) - '0');
                }
                if (!sawDigit || position >= shape.length() ||
                        shape.charAt(position++) != ':' ||
                        position + length > shape.length() ||
                        handleIndex >= handles.length) {
                    throw new IllegalArgumentException(
                        "invalid SLP predicate leaf descriptor");
                }
                String name = shape.substring(
                    position, position + length);
                position += length;
                return load("dev.slp.jvm.SlpCheckEval")
                    .getConstructor(
                        Bridge.class, long.class, String.class)
                    .newInstance(
                        Bridge.this,
                        Long.valueOf(handles[handleIndex++]), name);
            }
            if ((kind != 'A' && kind != 'O') ||
                    position >= shape.length() ||
                    shape.charAt(position++) != '(') {
                throw new IllegalArgumentException(
                    "invalid SLP predicate branch descriptor");
            }
            Object left = parseCheck();
            if (position >= shape.length() ||
                    shape.charAt(position++) != ',') {
                throw new IllegalArgumentException(
                    "missing SLP predicate branch separator");
            }
            Object right = parseCheck();
            if (position >= shape.length() ||
                    shape.charAt(position++) != ')') {
                throw new IllegalArgumentException(
                    "unterminated SLP predicate branch descriptor");
            }
            Class<?> branch = kind == 'A'
                ? checkAndClass : checkOrClass;
            return branch.getConstructor(checkClass, checkClass)
                .newInstance(left, right);
        }
    }

    private static final class Match<T extends Executable> {
        final T executable;
        final Object[] arguments;
        final int score;

        Match(T executable, Object[] arguments, int score) {
            this.executable = executable;
            this.arguments = arguments;
            this.score = score;
        }
    }

    private static final class Conversion {
        final Object value;
        final int score;

        Conversion(Object value, int score) {
            this.value = value;
            this.score = score;
        }

        static Conversion failure() {
            return new Conversion(NO_CONVERSION, Integer.MAX_VALUE);
        }
    }

    /** Identity-bearing carriers used only across the JNI collection seam. */
    public static final class ArrayValue {
        public final long identity;
        public Object[] elements;

        public ArrayValue(long identity, Object[] elements) {
            this.identity = identity;
            this.elements = elements == null ? new Object[0] : elements;
        }
    }

    public static final class HashValue {
        public final long identity;
        public Object[] entries;

        public HashValue(long identity, Object[] entries) {
            this.identity = identity;
            this.entries = entries == null ? new Object[0] : entries;
        }
    }
}
