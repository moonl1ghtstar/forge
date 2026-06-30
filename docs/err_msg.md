
# Error Msgs Forms:
```
<LEVEL>[<ERR_CODE>]: <ERR_MSG>
 --> <FILE_NAME>:<LINE>:<COLUMN>
       |
<LINE> | <SOURCE_LINE>
       | <POINTER>
       |
= <NOTE_KIND>: <NOTE_MSG>
```

# Levels:

## Error:
```
	error[<ERR_CODE>]: <ERR_MSG>
	 --> <FILE_NAME>:<LINE>:<COLUMN>
	       |
	<LINE> | <SOURCE_LINE>
	       | <POINTER>
	       |
	= <NOTE_KIND>: <NOTE_MSG>
```
## Warn:
```
    warn[<WARN_CODE>]: <WARN_MSG>
	 --> <FILE_NAME>:<LINE>:<COLUMN>
	       |
	<LINE> | <SOURCE_LINE>
	       | <POINTER>
	       |
	= <NOTE_KIND>: <NOTE_MSG>
```


# ERR Codes:

## ERR Codes:
### 100 Series – Lexical Errors (Lexer)  
  
| Code | Error Message              | Description                                                    |
| ---- | -------------------------- | -------------------------------------------------------------- |
| E001 | unexpected character: '%c' | An invalid character was encountered.                          |
| E002 | unterminated block comment | A `/* ... */` comment reached EOF before being closed.         |
| E003 | invalid numeric literal    | Malformed number, overflow, or invalid characters in a number. |
  
### 200 Series – Syntax Errors (Parser)  
  
| Code | Error Message                                                 | Description                                            |
| ---- | ------------------------------------------------------------- | ------------------------------------------------------ |
| E201 | expected '%s', found '%s'                                     | A required token was not found.                        |
| E202 | expected an identifier after 'function'                       | Invalid function declaration such as `function 123()`. |
| E203 | expected ')'                                                  | Missing closing parenthesis.                           |
| E204 | expected ';'                                                  | Missing statement terminator.                          |
| E205 | expected '{'                                                  | Missing block opening brace.                           |
| E206 | invalid assignment expression                                 | Missing expression after `=`.                          |
| E207 | only function declarations are allowed inside an extern block | Invalid item inside an `extern` block.                 |
  
### 300 Series – Semantic Errors (Sema)  
  
| Code | Error Message                                              | Description                                    |
| ---- | ---------------------------------------------------------- | ---------------------------------------------- |
| E301 | undefined variable '%s'                                    | The variable has not been declared.            |
| E302 | '%s' is not a function                                     | Attempted to call a non-function value.        |
| E303 | function '%s' expects %d argument(s), but %d were provided | Function argument count mismatch.              |
| E304 | duplicate definition of function '%s'                      | A function with the same name already exists.  |
| E305 | entry point 'main' was not found                           | The program does not define a `main` function. |
| E306 | 'return' cannot be used outside of a function              | `return` was used at the top level.            |
  
### 400 Series – Type Errors  
  
| Code | Error Message                            | Description                                            |
| ---- | ---------------------------------------- | ------------------------------------------------------ |
| E401 | type mismatch: expected '%s', found '%s' | The value's type does not match the expected type.     |
| E402 | cannot assign '%s' to '%s'               | Assignment between incompatible types.                 |
| E403 | invalid operand types for '%s'           | The operator does not support the given operand types. |
| E404 | cannot convert '%s' to '%s'              | Invalid implicit or explicit conversion.               |
  
## 500 Series – User-Defined Type Errors  
  
Description: Errors related to user-defined types such as `struct`, `class`, `enum`, and other custom types.  
  
| Code | Error Message                                    | Description                                                               |
| ---- | ------------------------------------------------ | ------------------------------------------------------------------------- |
| E501 | unknown type '%s'                                | The specified type does not exist or has not been declared.               |
| E502 | duplicate member '%s' in type '%s'               | A member with the same name is already defined in the type.               |
| E503 | type '%s' has no member '%s'                     | Attempted to access a member that does not exist.                         |
| E504 | missing initializer for member '%s'              | A required member was not initialized.                                    |
| E505 | duplicate initializer for member '%s'            | A member was initialized more than once.                                  |
| E506 | expected %d initializer(s), but %d were provided | The number of provided initializers does not match the number of members. |
| E507 | type '%s' cannot be instantiated                 | Attempted to instantiate a non-instantiable type.                         |
| E508 | invalid member access on type '%s'               | The requested operation is not valid for this type.                       |
| E509 | recursive type definition of '%s' is not allowed | The type directly or indirectly contains itself in an invalid way.        |
| E510 | duplicate type definition '%s'                   | A type with the same name has already been declared.                      |

### 600 Series – Module Errors  
  
| Code | Error Message                            | Description                                                |
| ---- | ---------------------------------------- | ---------------------------------------------------------- |
| E601 | failed to import module '%s'             | The requested module could not be found or loaded.         |
| E602 | symbol '%s' was not found in module '%s' | The imported module does not contain the requested symbol. |
| E603 | circular module dependency detected      | Modules import each other recursively.                     |
  
### 700 Series – Code Generation Errors  
  
| Code | Error Message                                              | Description                                                           |
| ---- | ---------------------------------------------------------- | --------------------------------------------------------------------- |
| E701 | function '%s' does not return a value on all control paths | Some execution paths reach the end of the function without returning. |
| E702 | unsupported feature in code generation                     | The backend does not yet support this construct.                      |
  
### 900 Series – Internal Compiler Errors  
  
| Code | Error Message                                     | Description                                      |
| ---- | ------------------------------------------------- | ------------------------------------------------ |
| E901 | internal compiler error: memory allocation failed | `malloc` or another allocation function failed.  |
| E902 | failed to write assembly output                   | File system error while writing the `.asm` file. |
| E999 | internal compiler error                           | An unexpected compiler failure occurred.         |
  
## Warning Codes  
  
| Code | Warning Message                                             | Description                                                 |
| ---- | ----------------------------------------------------------- | ----------------------------------------------------------- |
| W001 | variable '%s' is declared but never used                    | Unused local variable.                                      |
| W002 | unreachable code                                            | Code exists after `return`, `break`, etc.                   |
| W003 | empty if statement                                          | The `if` body is empty.                                     |
| W004 | nested block comments are not supported and will be ignored | `/* /* */` nesting is not supported.                        |
| W005 | variable '%s' shadows a previous declaration                | A local variable hides another variable with the same name. |
| W006 | function '%s' is declared but never used                    | Unused function.                                            |