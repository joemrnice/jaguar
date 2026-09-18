# Jaguar 1.1.0 Language Specification

This document serves as the authoritative specification for Jaguar 1.1.0 language core and type system.

## 1. Syntax & Core Data Types
Jaguar supports explicit static type annotations with local type inference:
- `string`: UTF-8 immutable strings with string interpolation (`"Hello ${name}"`)
- `num`: 64-bit signed integers
- `decimal`: double-precision IEEE 754 floating point numbers
- `bool`: boolean (`true`, `false`)
- `scifi`: scientific notation floating point
- `list` / `List<T>`: homogeneous or heterogeneous lists
- `data` / `Map<K, V>`: key-value dictionary mappings
- `struct`: user-defined named record values
- `enum`: strongly-typed enumeration values

## 2. Declarations
- `var x: num = 10;` — mutable variable declaration
- `fixed y: string = "const";` — immutable declaration
- `type Alias = TargetType;` — type aliases

## 3. Control Flow
- `if (cond) { ... } elif (cond) { ... } else { ... }`
- Ternary: `cond ? expr1 : expr2`
- Range check: `x <<< (low, high)`
- `loop (cond) { ... }`
- `do loop { ... } while (cond);`
- `for (item in list) { ... }`
- `iterate (collection, item) { ... }`
- `match (expr) { case pattern => stmt ... default => stmt }`

## 4. Object-Oriented Programming (OOP)
- `class Name [extends Parent] { ... }`
- `new ClassName(args)`
- `this` and `super` member access
- `static var` / `static fun` static class members
- `interface Name { fun method(): ReturnType; }`
- `class Implementation implements InterfaceName { ... }`
- Enforced `public` / `private` access modifiers

## 5. Structs & Enums
- `struct Point { x: num; y: num; }`
- `enum Color { Red, Green, Blue }`
- Enum member access: `Color.Red`

## 6. Error Handling
- `try { ... } catch (err) { ... }`
- `Result<T, E>` / `Error` typed error propagation

## 7. Async & Concurrency
- `async fun fetchData(): Task<string> { ... }`
- `await expr`
- `Task.all([...])` / `Task.race([...])`
- `worker.spawn` / `worker.pool`
