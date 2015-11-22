struct re2jit::native{

	re2::Prog *_prog;

	native(re2::Prog *prog) : _prog(prog) {}

	bool match(const re2::StringPiece &text, int flags, re2::StringPiece *groups, int ngroups){

		ssize_t i = _prog->start();
		ssize_t len = _prog->size();

		const char* p = text.data();

		
		//ssize_t text_len = text.size;

		/*auto anchor = re2::Prog::kAnchored;
		auto kind   = re2::Prog::kFirstMatch;

		if (!(flags & RE2JIT_ANCHOR_START))
			anchor = re2::Prog::kUnanchored;
		else if (flags & RE2JIT_ANCHOR_END)
			kind = re2::Prog::kFullMatch;*/

		//return _prog->SearchNFA(text, text, anchor, kind, groups, ngroups);;
		std::vector<int> nextq;
		std::vector<int> runq;
		std::vector<int> visit;
		std::stack<int> stk;
		runq.push_back(i);

		while(1) {
			++p;
			while(!runq.empty()){
				//step
				nextq.clear();
				visit.clear();
				for(auto iter: runq){
					stk.push(iter);
					while(!stk.empty()) {
						int i = stk.top();
						stk.pop();
						if(std::find(visit.begin(), visit.end(), i) == visit.end()){
							continue;
						}
						visit.push_back(i);
						re2::Prog::Inst *op = _prog->inst(i);

						switch (op->opcode()){

							case re2::kInstAltMatch:
							case re2::kInstAlt:
								stk.push(op->out1());
								stk.push(op->out());

							case re2::kInstByteRange:
								if(op->Matches(*p)){
									nextq.push_back(op->out());
								}
							case re2::kInstCapture:
								// нужно записать, что здесь начинается/заканчивается группа
								// op->cap() -- 2 * n для n-й открывающей скобки
								//              2 * n + 1 для закрывающей
								// op->out() -- когда отметили, переходим сюда

							case re2::kInstEmptyWidth:
								// нужно проверить флаги состояния
								// op->empty() -- какие должны быть выставлены
								//                это побитовое "или" констант:
								//     re2::kEmptyBeginLine
								//     re2::kEmptyEndLine
								//     re2::kEmptyBeginText
								//     re2::kEmptyEndText
								//     re2::kEmptyWordBoundary
								//     re2::kEmptyNonWordBoundary
								// op->out() -- если все верно, идем сюда   

							case re2::kInstNop:
								stk.push(op->out());

							case re2::kInstMatch:
								return true;

							case re2::kInstFail:
								// не получилось, пробуем другую ветку
								break;
						}
					}
				}
				//end of step
				swap(nextq, runq);
				nextq.clear();
			}
			break;
		}   
		return false;
	}
};
