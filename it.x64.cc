#include <sys/mman.h>
#include <iostream>

#include "asm64.h"

bool CheckMatched(char p, re2::Prog::Inst* op) {
    if(op->Matches(p))
        return true;
    else
        return false;
}

bool visit(char p, int i, int** visited){
    if(*(*visited + i)){
        return true;
    } else{
        (*(*visited + i))++;
        return false;
    }
}

struct re2jit::native
{
    //int * _start = nullptr; // начальное состояние
    //vector<void*> _matching; // допускающие состояния
    void * _code = nullptr;  // сгенерированный код
    void * _start = nullptr;
    size_t _size = 0;
    re2::Prog *_prog; //const re2::Prog *_prog;

    native(re2::Prog *prog) : _prog(prog)
    {
        // System V ABI:
        //   * первые 6 аргументов - rdi, rsi, rdx, rcx, r8, r9
        //   * остальные на стеке
        //   * возвращаемое значение - rax
        //   * регистры rax, rcx, rdx, rdi, rsi, r8, r9, r10 при вызовах не сохраняются
        //   * регистры rbx, rsp, rbp, r11-r15 -- сохраняются.
        //     сгенерированного кода это тоже касается. если испортить значение
        //     в регистре, поведение после ret будет непредсказуемым.
        //static const char* str = "HERE\n";
        // memset(&stack[-length], 'x', length);
        // code.mov (as::rsi, as::rdx) // arg1 = size
        //     .sub (as::rsi, as::rsp) // stack_ptr -= size
        //     .push(as::rsi) // stack_ptr -= 8
        //     .push(as::rdi) // stack_ptr -= 8
        //     .mov (as::rsp + 16, as::rdi) // arg1 = &stack + 16
        //     .push(as::rdi) // stack_ptr -= 8
        //     .mov (as::i32('x'), as::esi) // arg2 = 'x'
        //     .call(&memset)
        // // if (memcmp(rdi = input, rsi = &stack[-length], rcx = length) != 0) goto fail;
        //     .pop (as::rsi) // rsi = &stack_ptr на строчку
        //     .pop (as::rdi) // rdi = &text
        //     .pop (as::rcx) // rcx = size
        //     .repz().cmpsb()
        //     .mov (as::rsi + as::rcx, as::rsp)
        //     .jmp (fail, as::not_equal)
        //     // return 1;
        //     .mov (as::i8(1), as::eax)
        //     .ret ()
        // // fail: return 0;
        //     .mark(fail) 
        //     .xor_(as::eax, as::eax)
        //     .ret ();


    // rdi - текущий символ
    // rsi - конец следующей очереди тредов nextq
    // rdx - указатель на массив посещенных вершин
        as::code code;
        as::label fail, succeed, proceed;

        int n = prog->size();
        std::vector<as::label> labels(n);
        int start_off = -1;
        code.mark(proceed);
        for (int i = 0; i < n; i++) {
            if (i == prog->start()) {
                start_off = code.size();
            }
            code.push(as::rdi)
                .push(as::rsi)
                .mov(as::i32(i), as::rsi)
                .push(as::rdx)
                .call(visit)
                .pop(as::rdx)
                .pop(as::rsi)
                .pop(as::rdi)
                .cmp(as::i32(0), as::eax)
                .jmp(proceed, as::zero);
            auto op = prog->inst(i);
            code.mark(labels[i]);
            switch(op->opcode()){
                case re2::kInstAltMatch:
                case re2::kInstAlt:
                    code.push(as::rdi)
                        .push(as::rsi)
                        .push(as::rdx)
                        .call(labels[op->out()])
                        .pop(as::rdx)
                        .pop(as::rsi)
                        .pop(as::rdi)
                        .jmp(labels[op->out1()]);
                        break;

                case re2::kInstByteRange:
                    code.push(as::rdi)
                        .push(as::rsi)
                        .mov(op, as::rsi)
                        .push(as::rdx)
                        .call(CheckMatched)
                        .pop(as::rdx)
                        .pop(as::rsi)
                        .pop(as::rdi)
                        .test(as::i8(0), as::eax)
                        .jmp(fail, as::not_equal)
                        .jmp(labels[op->out()])
                        .mov(as::i32(op->out()), as::rdx)
                        .add(as::i8(8), as::rdx)
                        .ret();
                    break;                

                case re2::kInstEmptyWidth:
                    code.jmp(labels[op->out()]);
                        // .mov(as::i8(0), as::eax)
                        // .ret();
                    break;

                case re2::kInstNop:
                    code.jmp(labels[op->out()]);
                    break;
                case re2::kInstMatch:
                    code.mov(as::i32(1), as::eax)
                        .ret();
                    break;
                case re2::kInstFail:
                    code.jmp(fail);
                    break;
            }
        }

        code.mark(fail)
            .mov(as::i8(0),as::eax)
            .ret();

        size_t sz = code.size();
        char * tg = (char*)mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (tg == (void *) -1)
            throw std::runtime_error("mmap failed");

        if (!code.write(tg) || start_off < 0) {
            munmap(tg, sz);
            throw std::runtime_error("a label was used but not set");
        }

        _start = tg + start_off;

        FILE *f = fopen("asd.bin", "wb");
        fwrite(tg, 1, sz, f);
        fclose(f);

        if (mprotect(tg, sz, PROT_READ | PROT_EXEC) == -1) {
            munmap(tg, sz);
            throw std::runtime_error("can't change permissions");
        }

        _code = tg;
        _size = sz;
        //_f = label[prog->start()](tg);

    }

   ~native()
    {
        munmap(_code, _size);
    }

    bool match(const re2::StringPiece &text, int /* flags */,
                     re2::StringPiece * /* groups */, int /* ngroups */)
    {

        using jit_state_ptr = bool(*)(char, void**, int**);

        std::vector<jit_state_ptr> table(_prog->size(), 0);
        std::vector<jit_state_ptr> threads0(2 * _prog->size(), 0);
        std::vector<jit_state_ptr> threads1(2 * _prog->size(), 0);
        std::vector<int> visited(_prog->size(), 0);
        //std::cerr << "HERE: " << text << std::endl;
        jit_state_ptr* begin0 = &threads0[0];
        jit_state_ptr* begin1 = &threads1[0];
        jit_state_ptr* end0 = begin0;
        jit_state_ptr* end1 = begin1;

        int* visited_q = &visited[0];

        *begin0 = (jit_state_ptr)_start;
        end0 += 1;

        for (auto c : text) {
            jit_state_ptr* iter = begin0;
            while (iter != end0) {
                bool flag = (*iter)(c, (void**)end1, &visited_q); //((*state)begin0)(c, end0, end1);
                if(flag){
                    return true;
                }
                ++iter;
            }

            if (end1 == begin1) {
                return false;
            }

            std::swap(begin0, begin1);
            end0 = end1;
            end1 = begin1;
        }
      
        // см. it.vm.cc -- там описание StringPiece, Prog, flags и groups.
        return false;//
    }
};
