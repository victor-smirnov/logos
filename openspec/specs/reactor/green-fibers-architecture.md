# Green Fibers: Архитектура и Контракт с Компилятором

## Содержание

1. [Концепция Green Fibers](#концепция-green-fibers)
2. [Архитектура на уровне компилятора](#архитектура-на-уровне-компилятора)
3. [Контракт с компилятором](#контракт-с-компилятором)
4. [Сегментированные стеки](#сегментированные-стеки)
5. [Переключение контекста](#переключение-контекста)
6. [Builtins и их семантика](#builtins-и-их-семантика)
7. [Runtime библиотека](#runtime-библиотека)
8. [Примеры использования](#примеры-использования)

---

## Концепция Green Fibers

### Основная идея

**Green Fibers** — это система легковесных потоков выполнения (файберов) с динамически растущими сегментированными стеками. Ключевая особенность: файберы выполняются в одном системном потоке, но имеют независимые стеки, которые растут по мере необходимости.

### Функциональное окрашивание (Function Coloring)

Система использует концепцию **функционального окрашивания**:

- **`[[clang::green]]`** — функции, которые выполняются на сегментированном стеке файбера
- **`[[clang::red]]`** — функции, которые выполняются на обычном системном стеке

Это разделение критично, потому что:
- Green функции могут динамически расти (сегментированный стек)
- Red функции используют фиксированный системный стек
- Прямые вызовы между разными цветами запрещены (требуют специальных builtins)

### Зачем это нужно?

1. **Эффективность**: Файберы легче потоков (нет переключения ядра)
2. **Масштабируемость**: Можно создать миллионы файберов
3. **Контроль**: Кооперативная многозадачность вместо вытесняющей
4. **Память**: Стеки растут только при необходимости

---

## Архитектура на уровне компилятора

### 1. Атрибуты функций

#### Определение атрибутов

Атрибуты определены в `clang/include/clang/Basic/Attr.td`:

```tablegen
def Green : InheritableAttr {
  let Spellings = [GNU<"green">, CXX11<"clang", "green">];
  let Subjects = SubjectList<[Function, Namespace, CXXRecord]>;
  let Documentation = [GreenFibersDocs];
}

def Red : InheritableAttr {
  let Spellings = [GNU<"red">, CXX11<"clang", "red">];
  let Subjects = SubjectList<[Function, Namespace, CXXRecord]>;
  let Documentation = [RedFibersDocs];
}
```

#### Семантика атрибутов

- **Наследование**: Атрибуты наследуются от namespace/класса к функциям
- **По умолчанию**: Функции без атрибута считаются **red**
- **Проверки**: Sema проверяет, что нет прямых вызовов между green и red

#### Поддержка Лямбда-выражений

Атрибут `[[clang::green]]` может быть применен к лямбда-выражениям. Это помечает оператор вызова `operator()` лямбды как green-функцию.

**Синтаксис:**

1. **С выводимым типом возврата:**
   ```cpp
   auto green_lambda = []() [[clang::green]] {
       // Green код
   };
   ```

2. **С явным типом возврата:**
   Атрибут должен располагаться **перед** стрелкой `->` возвращаемого типа:
   ```cpp
   auto green_lambda = []() [[clang::green]] -> int {
       return 42;
   };
   ```

### 3. Вывод атрибутов (Inference)

Лямбды, определенные внутри функции, помеченной атрибутом `[[clang::green]]`, автоматически наследуют этот атрибут (становятся green-лямбдами). Это позволяет вызывать green-функции из тела лямбды без явного указания атрибута.

Можно явно переопределить поведение, указав `[[clang::red]]`.

```cpp
[[clang::green]] void foo() {
    // Неявно green
    auto l1 = []() {
        other_green_fn(); // OK
    };

    // Явно red (переопределение)
    auto l2 = []() [[clang::red]] {
        // other_green_fn(); // Ошибка компиляции
    };
}
```

#### Важные особенности Clang 19+

1. **Неявный вывод и `std::function`**:
   Так как лямбды внутри green-функций становятся неявно green, они несовместимы с `std::function` (который ожидает red-функцию), если они не захватывают контекст. Если вы передаете колбэк в API, ожидающее `std::function` (например, в event loop), вы **должны** явно пометить лямбду как `[[clang::red]]`.

   ```cpp
   // ОШИБКА: компилятор сделает лямбду Green, но register_read ждет std::function<void()>
   loop->register_read(fd, [=]() { ... }); 

   // ИСПРАВЛЕНИЕ:
   loop->register_read(fd, [=]() [[clang::red]] { ... });
   ```

2. **Вложенные Green Лямбды**:
   В текущей версии компилятора существует ограничение: атрибут `[[clang::green]]` может некорректно распространяться на вложенные лямбды (лямбда внутри лямбды).
   
   *Workaround*: Используйте именованные локальные структуры (functors) с аннотированным `operator()`, если сталкиваетесь с ошибками компиляции во вложенных лямбдах.

   ```cpp
   // Вместо вложенной лямбды:
   struct Task {
       [[clang::green]] void operator()() { ... }
   };
   sched.spawn(Task{...});
   ```

### 2. CodeGen для Green функций

#### Split-Stack атрибут

В `clang/lib/CodeGen/CodeGenModule.cpp`:

```cpp
void CodeGenModule::SetFunctionAttributes(GlobalDecl GD, llvm::Function *F, 
                                          const CGFunctionInfo &MInfo) {
    const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());
    
    if (FD->hasAttr<GreenAttr>()) {
        // Добавляем split-stack атрибут для LLVM
        F->addFnAttr("split-stack");
    }
}
```

#### Что делает `split-stack`?

LLVM добавляет в начало каждой green функции **проверку стека**:

```asm
# Псевдокод того, что генерирует LLVM
function_start:
    # Проверка: достаточно ли стека?
    if (current_stack_pointer < stack_limit) {
        # Вызов runtime для выделения нового сегмента
        call __morestack(frame_size, arg_size)
    }
    # Обычный код функции
    ...
```

#### Флаг `-fsplit-stack` НЕ требуется

**Важно**: Флаг компилятора `-fsplit-stack` **НЕ нужен** и **НЕ рекомендуется** для работы с green fibers.

**Почему:**
1. Компилятор автоматически добавляет атрибут `split-stack` к green функциям на основе атрибута `[[clang::green]]`
2. Мы предоставляем свою реализацию `__morestack` и `__morestack_release_segments` в `green_morestack.S`
3. Флаг `-fsplit-stack` предназначен для стандартного механизма GCC split-stack, который:
   - Добавляет `split-stack` ко **всем** функциям (не только green)
   - Ожидает стандартную реализацию из libgcc
   - Может конфликтовать с нашей реализацией

**Что нужно для компиляции:**
- Использовать модифицированный Clang с поддержкой green fibers
- Указать атрибут `[[clang::green]]` для green функций
- Слинковать с нашим runtime (`green_morestack.S`, `green_fiber.cpp`, и т.д.)

**Итог**: Просто используйте `[[clang::green]]` — компилятор автоматически добавит нужный атрибут `split-stack` к LLVM функции.

#### Флаг `-fno-green-code`

**Назначение**: Флаг `-fno-green-code` позволяет компилировать green-код как обычный red-код, отключая все механизмы green fibers.

**Использование:**
```bash
clang++ -fno-green-code source.cpp
```

**Что делает флаг:**

1. **Отключает split-stack**: Green-функции компилируются без атрибута `split-stack`, как обычные red-функции
2. **Отключает семантические проверки**: Разрешает прямые вызовы green-функций из red-функций без использования `__builtin_green_call`
3. **Отключает автоматическую обёртку**: Вызовы red-функций из green-функций не оборачиваются в `__builtin_red_call`

**Когда использовать:**

- **Отладка**: Для упрощения отладки green-кода без overhead сегментированных стеков
- **Тестирование**: Для проверки логики кода без зависимости от runtime библиотеки
- **Совместимость**: Для компиляции green-кода в окружениях, где runtime недоступен

**Пример:**

```cpp
[[clang::green]] void green_func() {
    // Эта функция будет скомпилирована как обычная red-функция
    // при использовании -fno-green-code
}

void red_func() {
    green_func();  // ✅ Разрешено с -fno-green-code
}
```

**Важно**: При использовании `-fno-green-code` green-функции теряют способность динамически расти, и весь код выполняется на обычном системном стеке. Это может привести к переполнению стека, если green-функции используют большие локальные массивы или глубокую рекурсию.

### 3. Семантические проверки

В `clang/lib/Sema/SemaExpr.cpp`:

```cpp
// Запрет прямых вызовов green из red
if (Callee->hasAttr<GreenAttr>() && !Caller->hasAttr<GreenAttr>()) {
    // Вызов green из red - требуется __builtin_green_call
    Diag(Call->getBeginLoc(), diag::err_green_call_from_red);
    return ExprError();
}

// Автоматическая обёртка вызовов red из green
// Вызовы red функций из green функций автоматически трансформируются в __builtin_red_call
// на этапе Sema в BuildResolvedCallExpr
```

---

## Контракт с компилятором

### Общий принцип

Компилятор **НЕ ЗНАЕТ** о структуре `fiber_context` или размере стека. Он работает только с:

1. **Атрибутами функций** (`[[clang::green]]` / `[[clang::red]]`)
2. **Builtins** для межцветных вызовов
3. **LLVM split-stack механизмом** (через `__morestack`)

### Контракт для Green функций

#### 1. Split-Stack механизм

**Компилятор гарантирует:**
- Добавляет `split-stack` атрибут к LLVM функции
- Генерирует проверку стека в прологе функции
- Вызывает `__morestack` при нехватке стека

**Runtime должен предоставить:**
- Функцию `__morestack(frame_size, arg_size)` → возвращает новый указатель стека
- TLS переменную для текущего `stack_limit`
- Функцию `__morestack_release_segments()` для освобождения сегментов

#### 2. Структура сегмента стека

Runtime определяет структуру `stack_segment`:

```c
struct stack_segment {
    struct stack_segment *prev_segment;  // Связанный список сегментов
    struct green_allocator *allocator;   // Аллокатор для этого сегмента
    size_t segment_size;                 // Размер сегмента
    void *stack_limit;                   // Лимит стека (для проверки)
    void *return_address;                // Адрес возврата
    char data[];                         // Данные стека
};
```

**Важно**: Компилятор не знает об этой структуре! Он только вызывает `__morestack`.

### Контракт для Builtins

#### `__builtin_green_call`

**Сигнатура:**
```c
void __builtin_green_call(void* fiber_context, void(*fn)(), ...args);
```

**Семантика:**
- Принимает указатель на `fiber_context` (тип `void*` — компилятор не знает структуру!)
- Вызывает green функцию `fn` на стеке файбера
- Блокирует до завершения функции

**CodeGen:**
```cpp
// В clang/lib/CodeGen/CGBuiltin.cpp
case Builtin::BI__builtin_green_call: {
    // Игнорируем context (side effect only)
    EmitScalarExpr(E->getArg(0));
    
    // Обычный вызов функции
    const Expr *FnArg = E->getArg(1);
    CGCallee Callee = EmitCallee(FnArg);
    // ... подготовка аргументов и вызов
    return EmitCall(FnInfo, Callee, ReturnValueSlot(), Args);
}
```

**Контракт:**
- Компилятор генерирует обычный вызов функции
- Runtime должен обеспечить, что вызов происходит на правильном стеке
- `fiber_context` используется runtime, но компилятор его не анализирует

#### `__builtin_red_call`

**Сигнатура:**
```c
T __builtin_red_call(T(*fn)(...), ...args);
```

**Семантика:**
- Вызывает red функцию из green контекста
- Переключается на системный стек перед вызовом
- Возвращается на стек файбера после вызова

**CodeGen:**
```cpp
case Builtin::BI__builtin_red_call: {
    // 1. Сохраняем текущий стек (fiber stack)
    Value *SavedStack = Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::stacksave, {CGM.Int8PtrTy}));
    
    // 2. Переключаемся на системный стек
    GlobalVariable *SystemStack = CGM.getModule().getNamedGlobal(
        "__green_fiber_system_stack");
    // Загружаем системный стек из TLS
    Value *SystemStackPtr = Builder.CreateLoad(SystemStack);
    // Устанавливаем как текущий стек (через inline asm или intrinsic)
    
    // 3. Подготавливаем аргументы и вызываем функцию
    // (аргументы теперь на системном стеке!)
    CallArgList Args;
    EmitCallArgs(Args, FPT, ...);
    Value *Result = EmitCall(FnInfo, Callee, ReturnValueSlot(), Args);
    
    // 4. Восстанавливаем стек файбера
    Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::stackrestore, {CGM.Int8PtrTy}),
        SavedStack);
    
    return Result;
}
```

**Контракт:**
- Компилятор использует `llvm.stacksave`/`llvm.stackrestore` для сохранения стека
- Runtime должен предоставить TLS переменную `__green_fiber_system_stack`
- Runtime должен обеспечить корректное переключение стека

### Контракт для Runtime

#### Обязательные функции

1. **`__morestack`** (генерируется LLVM, но реализуется runtime):
   ```c
   void* __morestack(size_t frame_size, size_t arg_size);
   ```
   - Вызывается автоматически LLVM при нехватке стека
   - Должна выделить новый сегмент через `green_fiber_allocate_segment`
   - Возвращает новый указатель стека

2. **`__morestack_release_segments`**:
   ```c
   void __morestack_release_segments(void);
   ```
   - Освобождает сегменты стека при возврате из функции

3. **TLS переменная**:
   ```c
   extern __thread void* __green_fiber_system_stack;
   ```
   - Хранит указатель на системный стек
   - Используется `__builtin_red_call` для переключения

#### Структура `fiber_context`

**Важно**: Компилятор **НЕ ЗАВИСИТ** от структуры `fiber_context`!

```cpp
class fiber_context {
private:
    void* stack_segment_ptr_;  // Указатель на текущий сегмент стека
    green_allocator* allocator_;
    fiber_context* caller_;
    size_t total_stack_used_;
    
    enum class state_t { idle, running, suspended, finished };
    state_t state_;
    
    struct context_frame { /* регистры */ };
    context_frame context_;
    // ...
};
```

Компилятор работает только с `void*` указателем на контекст, не зная его структуры.

---

## Сегментированные стеки

### Как это работает

#### 1. Инициализация файбера

```cpp
fiber_context ctx(allocator, 64 * 1024);  // 64KB начальный стек

// Внутри конструктора:
stack_segment* initial = allocator->allocate(64 * 1024);
stack_segment_ptr_ = initial;
```

#### 2. Выполнение green функции

```cpp
[[clang::green]] void my_function(int x) {
    int local[1000];  // Большой локальный массив
    // ...
}
```

**Что происходит:**

1. LLVM генерирует проверку в прологе:
   ```asm
   # Проверка: достаточно ли стека?
   mov %rsp, %rax
   cmp %fs:0x70, %rax  # stack_limit в TLS
   jae .enough_stack
   
   # Недостаточно - вызываем __morestack
   mov $frame_size, %edi
   mov $arg_size, %esi
   call __morestack
   # __morestack возвращает новый %rsp
   mov %rax, %rsp
   .enough_stack:
   ```

2. `__morestack` вызывает runtime:
   ```c
   void* __morestack(size_t frame_size, size_t arg_size) {
       fiber_context* ctx = tls_current_fiber;
       
       // Выделяем новый сегмент
       stack_segment* new_seg = 
           ctx->allocator->allocate(frame_size + arg_size);
       
       // Связываем с предыдущим
       new_seg->prev_segment = ctx->stack_segment_ptr_;
       ctx->stack_segment_ptr_ = new_seg;
       
       // Возвращаем новый указатель стека
       return new_seg->data + new_seg->segment_size;
   }
   ```

3. Функция выполняется на новом сегменте

#### 3. Возврат из функции

При возврате LLVM вызывает `__morestack_release_segments()`:

```c
void __morestack_release_segments(void) {
    fiber_context* ctx = tls_current_fiber;
    stack_segment* current = ctx->stack_segment_ptr_;
    
    if (current->prev_segment) {
        // Освобождаем текущий сегмент
        ctx->allocator->deallocate(current);
        // Возвращаемся к предыдущему
        ctx->stack_segment_ptr_ = current->prev_segment;
    }
}
```

### Связанный список сегментов

```
[Segment 3] -> [Segment 2] -> [Segment 1] -> NULL
   ^
   |
current (stack_segment_ptr_)
```

Каждый сегмент указывает на предыдущий, образуя стек сегментов.

---

## Переключение контекста

### Асимметричные файберы

#### `fiber_context::call()`

```cpp
template<typename Fn, typename... Args>
void fiber_context::call(Fn&& fn, Args&&... args) {
    // Сохраняем функцию для выполнения
    pending_invocation_ = make_unique<invocation<Fn, Args...>>(...);
    
    // Устанавливаем caller
    caller_ = fiber_context::current();
    
    // Переключаемся на этот файбер
    switch_to(this);
    
    // Когда вернемся сюда, функция выполнена
}
```

#### `fiber_context::suspend()`

```cpp
static void fiber_context::suspend() {
    fiber_context* current = current();
    
    // Сохраняем состояние
    current->state_ = state_t::suspended;
    
    // Возвращаемся к caller
    switch_to(current->caller_);
    
    // Когда вернемся, продолжаем выполнение
    current->state_ = state_t::running;
}
```

### Низкоуровневое переключение

#### Assembly контекст-свитчер

В `green_context_switch.S`:

```asm
__green_fiber_context_switch:
    # Сохраняем регистры в from_frame
    mov %r15, 0(%rdi)
    mov %r14, 8(%rdi)
    # ... остальные callee-saved регистры
    mov %rsp, 48(%rdi)
    mov %rip, 56(%rdi)  # Сохраняем адрес возврата
    
    # Восстанавливаем регистры из to_frame
    mov 0(%rsi), %r15
    mov 8(%rsi), %r14
    # ...
    mov 48(%rsi), %rsp
    mov 56(%rsi), %rax
    jmp *%rax  # Переход на сохраненный адрес
```

#### Структура `context_frame`

```cpp
struct alignas(16) context_frame {
    void* r15;
    void* r14;
    void* r13;
    void* r12;
    void* rbx;
    void* rbp;
    void* rsp;  // Указатель стека
    void* rip;  // Инструкция возврата
};
```

**Важно**: Сохраняются только **callee-saved** регистры. Caller-saved регистры (rax, rcx, rdx, rsi, rdi, r8-r11) не сохраняются, так как они могут быть изменены вызываемой функцией.

---

## Builtins и их семантика

### `__builtin_green_call`

#### Использование

```cpp
void* ctx = ...;  // fiber_context*
[[clang::green]] void my_function(int x);

__builtin_green_call(ctx, my_function, 42);
```

#### Что генерирует компилятор

```llvm
; Псевдокод LLVM IR
%ctx = load i8*, i8** %context_ptr
call void @llvm.donothing(i8* %ctx)  ; Side effect для context
call void @my_function(i32 42)       ; Обычный вызов
```

#### Что делает runtime

Runtime должен перехватить вызов и выполнить его на стеке файбера. Но в текущей реализации `__builtin_green_call` просто выполняет обычный вызов — это означает, что вызов должен происходить **уже на стеке файбера**.

**Контракт:**
- Вызывающий код должен быть на стеке файбера (через `fiber_context::call()`)
- `__builtin_green_call` используется только для семантической проверки

### `__builtin_red_call`

#### Использование

```cpp
[[clang::green]] void green_function() {
    void red_function(int x);
    
    __builtin_red_call(red_function, 42);
}
```

#### Что генерирует компилятор

```llvm
; Псевдокод LLVM IR
%saved_stack = call i8* @llvm.stacksave()
%system_stack = load i8*, i8** @__green_fiber_system_stack
; Переключение на системный стек (через inline asm или runtime call)
call void @red_function(i32 42)
call void @llvm.stackrestore(i8* %saved_stack)
```

#### Что делает runtime

1. Сохраняет текущий стек файбера через `llvm.stacksave`
2. Загружает системный стек из TLS
3. Переключается на системный стек
4. Выполняет red функцию
5. Восстанавливает стек файбера через `llvm.stackrestore`

**Критично**: Аргументы для red функции должны быть подготовлены **после** переключения на системный стек, чтобы они были на правильном стеке.

#### Оптимизации

В O0-коде последовательность `stacksave → загрузка системного стека → call → stackrestore` всегда присутствует вокруг вызова `__builtin_red_call`, даже если сама red-функция тривиальна. На оптимизационных уровнях (`-O1` и выше) LLVM сначала заинлайнит тело red-функции, а затем удалит сам "мост": если после inlining не осталось отдельного вызова, то инструкции переключения стека тоже исчезают. В итоге для маленьких red-функций, вызываемых из green-кода, весь overhead может быть оптимизирован в ноль — в IR остаётся только полезная арифметика без `llvm.stacksave/stackrestore`.

---

## Runtime библиотека

### Основные компоненты

#### 1. `fiber_context`

Высокоуровневый API для управления файберами:

```cpp
class fiber_context {
public:
    // Создание файбера
    fiber_context(green_allocator* alloc, size_t stack_size);
    
    // Вызов функции на файбере
    template<typename Fn, typename... Args>
    void call(Fn&& fn, Args&&... args);
    
    // Приостановка/возобновление
    static void suspend();
    void resume();
    
    // Состояние
    static fiber_context* current();
    bool empty() const;
};
```

#### 2. Аллокатор сегментов

```c
struct green_allocator {
    const struct green_allocator_vtable *vtable;
    void *user_data;
};

struct green_allocator_vtable {
    stack_segment* (*allocate)(green_allocator*, size_t, size_t);
    void (*deallocate)(green_allocator*, stack_segment*);
};
```

#### 3. C API для LLVM

```c
// Вызывается из __morestack
void* green_fiber_allocate_segment(
    size_t frame_size,
    size_t arg_size,
    void* return_address
);

// Вызывается из __morestack_release_segments
void green_fiber_release_segment(void);
```

### Интеграция с LLVM

#### TLS для stack_limit

LLVM использует TLS для хранения `stack_limit`. На x86-64 это обычно `%fs:0x70`.

Runtime должен установить это значение при переключении на файбер:

```cpp
void switch_to_fiber(fiber_context* target) {
    stack_segment* seg = target->stack_segment_ptr_;
    // Устанавливаем stack_limit в TLS
    // (зависит от платформы, может требовать системных вызовов)
    set_tls_stack_limit(seg->stack_limit);
}
```

---

## Примеры использования

### Базовый пример

```cpp
#include "green_fiber.h"
#include "green_allocator.h"

[[clang::green]] void fiber_task(int id) {
    for (int i = 0; i < 10; ++i) {
        printf("Fiber %d: %d\n", id, i);
        green::fiber_context::suspend();
    }
}

int main() {
    green_allocator* alloc = green_allocator_create_simple(64 * 1024);
    
    green::fiber_context fiber1(alloc, 64 * 1024);
    green::fiber_context fiber2(alloc, 64 * 1024);
    
    // Запускаем файберы
    fiber1.call(fiber_task, 1);
    fiber2.call(fiber_task, 2);
    
    // Переключаемся между ними
    fiber1.resume();
    fiber2.resume();
    fiber1.resume();
    // ...
    
    green_allocator_destroy(alloc);
    return 0;
}
```

### Использование builtins

```cpp
[[clang::red]] void system_function(int x) {
    printf("System function: %d\n", x);
}

[[clang::green]] void green_function() {
    // Вызов red функции из green
    // ✅ Автоматически оборачивается в __builtin_red_call компилятором!
    system_function(42);  // Компилятор автоматически обернёт это в __builtin_red_call
    
    // Явный __builtin_red_call тоже работает (но не обязателен):
    __builtin_red_call(system_function, 42);
}

int main() {
    green_allocator* alloc = green_allocator_create_simple(64 * 1024);
    green::fiber_context fiber(alloc, 64 * 1024);
    
    // Вызов green функции
    fiber.call(green_function);

    // Использование green лямбды
    auto lambda = []() [[clang::green]] {
        printf("Hello from green lambda!\n");
    };
    fiber.call(lambda);
    
    green_allocator_destroy(alloc);
    return 0;
}
```

---

## Резюме контракта

### Компилятор гарантирует:

1. ✅ Добавляет `split-stack` атрибут к green функциям
2. ✅ Генерирует проверки стека и вызовы `__morestack`
3. ✅ Запрещает прямые вызовы между green и red
4. ✅ Генерирует код для `__builtin_green_call` и `__builtin_red_call`
5. ✅ **НЕ ЗАВИСИТ** от структуры `fiber_context`

### Runtime должен предоставить:

1. ✅ Функции `__morestack` и `__morestack_release_segments`
2. ✅ TLS переменную `__green_fiber_system_stack`
3. ✅ Реализацию `fiber_context` с сегментированными стеками
4. ✅ Контекст-свитчер для переключения между файберами
5. ✅ Аллокатор сегментов стека

### Независимость:

- ✅ Компилятор работает с `void*` указателями на контекст
- ✅ Структура `fiber_context` может изменяться без перекомпиляции компилятора
- ✅ Размер стека определяется runtime, не компилятором
- ✅ Аллокатор сегментов полностью под контролем runtime

---

## Дополнительные детали

### Feature Detection

Компилятор предоставляет макросы для проверки поддержки:

```cpp
#if __has_feature(green_fibers)
    // Green Fibers поддерживаются
#endif

#if __has_builtin(__builtin_green_call)
    // Builtin доступен
#endif
```

### Ограничения

1. **Однопоточность**: Файберы всегда выполняются в одном системном потоке (это архитектурное решение, а не ограничение)
2. **Платформа**: Текущая реализация для x86-64 Linux

### LTO

Link Time Optimization не ломает соглашения между зелёным и красным стеком:

* `[[clang::green]]` / `[[clang::red]]` атрибуты обрабатываются на этапе семантики, поэтому LTO видит уже готовые вызовы `__builtin_red_call` / `__builtin_green_call`.
* `__builtin_red_call` использует обычные LLVM intrinsic’и `llvm.stacksave` / `llvm.stackrestore` и TLS глобал `__green_fiber_system_stack`; все эти символы имеют external linkage и не «оптимизируются в ноль».
* Даже если LTO инлайнит часть функций, переключение стека остаётся вокруг вызова, потому что intrinsic нельзя выкинуть без изменения семантики.
* Единственное требование — собирать рантайм вместе с LTO (чтобы `__green_fiber_system_stack`, `green_context_switch.S`, `__morestack` и т.д. присутствовали в итоговом модуле).

Таким образом, использование LTO не требует дополнительной поддержки: достаточно подключить рантайм и следовать описанному контракту.

### Будущие улучшения

1. Поддержка других архитектур (ARM, RISC-V)
2. Интеграция с асинхронным IO

---

*Документ создан для понимания архитектуры Green Fibers и контракта между компилятором и runtime библиотекой.*

