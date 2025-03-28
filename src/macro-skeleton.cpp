// A clang-based command line tool that removes all non-macro code and comments from a C file
// leaving only the preprocessor directives

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

static cl::OptionCategory MacroSkeletonCategory("MacroSkeleton Options");
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

class MacroSkeletonAction : public ASTFrontendAction {
protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef) override {
        this->CI = &CI;
        RewriterForFile = std::make_unique<Rewriter>(CI.getSourceManager(), CI.getLangOpts());
        return std::make_unique<ASTConsumer>();
    }

    void EndSourceFileAction() override {
        SourceManager &SM = RewriterForFile->getSourceMgr();
        const auto FID = SM.getMainFileID();
        
        // Step 1: Remove comments
        Lexer Lex(SM.getLocForStartOfFile(FID), CI->getLangOpts(),
                SM.getBufferOrNone(FID)->getBufferStart(), 
                SM.getBufferOrNone(FID)->getBufferStart(),
                SM.getBufferOrNone(FID)->getBufferEnd());
        Lex.SetCommentRetentionState(true);

        CommentRewriter CR(*RewriterForFile);
        Token Tok;
        while (!Lex.LexFromRawLexer(Tok)) {
            if (Tok.is(tok::comment)) {
                CR.processComment(SourceRange(Tok.getLocation(), Tok.getEndLoc()), SM);
            }
        }

        // Get comment-removed code
        std::string IntermediateCode;
        llvm::raw_string_ostream RSO(IntermediateCode);
        RewriterForFile->getEditBuffer(FID).write(RSO);
        RSO.flush();

        // Step 2: Remove non-preprocessor code
        SmallVector<StringRef, 32> Lines;
        StringRef(IntermediateCode).split(Lines, "\n");
        
        std::string FinalOutput;
        bool InContinuation = false;
        for (const auto &Line : Lines) {
            const size_t LineLen = Line.size();
            StringRef Trimmed = Line.ltrim();

            bool IsPreprocessor = InContinuation || (!Trimmed.empty() && Trimmed[0] == '#');
            
            if (IsPreprocessor) {
                FinalOutput += Line.str();
            } else {
                FinalOutput.append(LineLen, ' ');
            }
            FinalOutput += '\n';

            // Check for line continuation
            size_t LastNonSpace = Line.find_last_not_of(" \t");
            InContinuation = (LastNonSpace != StringRef::npos && Line[LastNonSpace] == '\\');
        }

        llvm::outs() << FinalOutput;
    }

private:
    std::unique_ptr<Rewriter> RewriterForFile;
    CompilerInstance *CI = nullptr;
};

int main(int argc, const char **argv) {
    auto OptionsParser = CommonOptionsParser::create(argc, argv, MacroSkeletonCategory);
    if (!OptionsParser) {
        llvm::errs() << OptionsParser.takeError();
        return 1;
    }
    ClangTool Tool(OptionsParser->getCompilations(), OptionsParser->getSourcePathList());
    return Tool.run(newFrontendActionFactory<MacroSkeletonAction>().get());
}
