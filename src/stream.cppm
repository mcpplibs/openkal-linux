// openkal.stream --- the module name applications import.
//
// The name is supplied by the implementation rather than by the specification
// package, and the reason is a property of the language rather than a
// preference. Argument-dependent lookup does not reach a module the translation
// unit has not imported, so an implementation's optional operations are
// invisible unless they are declared in the module the application imports.
//
// The implementation is therefore obliged to add nothing else here. What it may
// add is bounded by the language --- redeclaring a type imported from the
// specification is rejected by the compiler --- and the remaining freedom, the
// addition of overloads, is bounded by conformance, which compares the exported
// name set of this module against the specification.
//
// This implementation adds no overloads. It does not provide vectored writes,
// and the absence is expressed by silence rather than by a failing definition.
export module openkal.stream;
export import openkal.decl.stream;
