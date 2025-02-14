#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/CommandLine.h"

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

static cl::OptionCategory MyToolCategory("Remove Comments Tool");
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

class CommentRewriter {
public:
    CommentRewriter(Rewriter &R) : Rewrite(R) {}

    void processComment(SourceRange Range, SourceManager &SM) {
        SourceLocation Loc = Range.getBegin();
        SourceLocation EndLoc = Range.getEnd();

        while (Loc <= EndLoc) {
            if (Loc.isMacroID()) {
                Loc = SM.getExpansionLoc(Loc);
                EndLoc = SM.getExpansionLoc(EndLoc);
            }

            bool Invalid = false;
            const char *Char = SM.getCharacterData(Loc, &Invalid);
            if (!Invalid && *Char != '\n') {
                Rewrite.ReplaceText(Loc, 1, " ");
            }
            Loc = Loc.getLocWithOffset(1);
        }
    }

private:
    Rewriter &Rewrite;
};

class RemoveCommentsAction : public ASTFrontendAction {
protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef) override {
        this->CI = &CI;
        RewriterForFile = std::make_unique<Rewriter>(CI.getSourceManager(), CI.getLangOpts());
        return std::make_unique<ASTConsumer>();
    }

    void EndSourceFileAction() override {
        SourceManager &SM = RewriterForFile->getSourceMgr();
        const auto FID = SM.getMainFileID();
        const auto *Buffer = SM.getBufferOrNone(FID).getPointer();
        Lexer Lex(SM.getLocForStartOfFile(FID), CI->getLangOpts(),
                Buffer->getBufferStart(), Buffer->getBufferStart(), Buffer->getBufferEnd());
        Lex.SetCommentRetentionState(true);

        CommentRewriter CR(*RewriterForFile);
        Token Tok;
        while (!Lex.LexFromRawLexer(Tok)) {
            if (Tok.is(tok::comment)) {
                CR.processComment(SourceRange(Tok.getLocation(), Tok.getEndLoc()), SM);
            }
        }

        RewriterForFile->getEditBuffer(FID).write(outs());
    }

    std::unique_ptr<Rewriter> RewriterForFile;
    CompilerInstance *CI;
};

int main(int argc, const char **argv) {
    auto OptionsParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
    if (!OptionsParser) {
        llvm::errs() << OptionsParser.takeError();
        return 1;
    }
    ClangTool Tool(OptionsParser->getCompilations(), OptionsParser->getSourcePathList());
    return Tool.run(newFrontendActionFactory<RemoveCommentsAction>().get());
}
