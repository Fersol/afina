#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
	// variable on stack
	char VarOnStack;
    ctx.Low = &VarOnStack;
    ctx.Hight = StackBottom;
    size_t size = ctx.Hight - ctx.Low;
    
    //resize stack if needed
    if (size > std::get<1>(ctx.Stack))
    {
        std::get<0>(ctx.Stack) = std::move(new char[size]);
        std::get<1>(ctx.Stack) = size;
    }

    memcpy(std::get<0>(ctx.Stack), ctx.Low, size);
}

void Engine::Restore(context &ctx) {
	char VarOnStack;
	// extend space
    if (&VarOnStack >= ctx.Low) {
    	Restore(ctx);
    }

    memcpy(ctx.Low, std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
    // transfer itself
    longjmp(ctx.Environment, 1);
}

void Engine::yield() {
	context *new_routine = alive;

	if (new_routine == nullptr) {
    	return;
    }

    if (new_routine == cur_routine) {
        if (new_routine->next != nullptr)
        {
            new_routine = new_routine->next;
        }
    }  

    sched(new_routine);
}

void Engine::sched(void *routine_) {
	if (routine_ == nullptr){
		if(cur_routine != nullptr){
			return;
		}
		else{
			yield();
		}
	}
	
	context *ctx = (context*) routine_;

    if (cur_routine != nullptr) {
    	// will later be transferred
    	if (setjmp(cur_routine->Environment) != 0) {
    		return;
    	}
    	Store(*cur_routine);
    }

    cur_routine = ctx;
    Restore(*cur_routine);
}

} // namespace Coroutine
} // namespace Afina
