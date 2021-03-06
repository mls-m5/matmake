#pragma once

#include "buildtype.h"
#include "main/token.h"
#include <memory>
#include <set>

class IFiles;
class IBuildRule;
class IBuildTarget;

class IDependency {
public:
    virtual ~IDependency() = default;

    //! Transform the source file to the output file
    //! Example do the compiling, copying or linking
    //! Called from build-rules work() function
    //! @return string to be written in verbose mode
    [[nodiscard]] virtual std::string work(const IFiles &files,
                                           class IThreadPool &pool) = 0;

    //! Remove all output files
    virtual void clean(const IFiles &files) = 0;

    // -------------------------- Other functions -----------------------------

    //! Tell a dependency that the built of this file is finished
    //! Set pruned to true if
    virtual void notice(IDependency *d, class IThreadPool &pool) = 0;

    //! A subscriber is a dependency that want a notice when the file is built
    virtual void sendSubscribersNotice(class IThreadPool &pool) = 0;

    virtual IBuildRule *parentRule() = 0;
    virtual void parentRule(IBuildRule *) = 0;

    // ------------------------------------------------------------------------

    virtual void addSubscriber(IDependency *s) = 0;

    //! Add a file that this file will wait for
    virtual void addDependency(IDependency *file) = 0;
    virtual const std::set<class IDependency *> dependencies() const = 0;

    //! Remove fresh dependencies from dependency tree
    virtual void prune() = 0;

    virtual const IBuildTarget *target() const = 0;

    // ------------------------------------------------------------------------

    // Sets and gets the state of the output file
    virtual void dirty(bool) = 0;
    virtual bool dirty() const = 0;

    //! Get the time when the file was latest changed
    //! 0 means that the file is not built at all
    virtual time_t changedTime(const IFiles &files) const = 0;
    virtual time_t inputChangedTime(const IFiles &files) const = 0;

    //! The path to where the target will be built
    virtual Token output() const = 0;
    virtual void output(Token value) = 0;

    //! The main target and implicit targets (often dependency files)
    virtual const std::vector<Token> &outputs() const = 0;

    //! If the file should be used in the link step
    //! This should be false for pcm files or copied resource files
    virtual bool includeInBinary() const = 0;

    virtual BuildType buildType() const = 0;

    virtual void linkString(Token token) = 0;
    virtual Token linkString() const = 0;

    virtual void input(Token in) = 0;
    virtual std::vector<Token> inputs() const = 0;
    virtual Token input() const = 0;
    virtual void depFile(Token file) = 0;
    virtual Token depFile() const = 0;
    virtual Token command() const = 0;
    virtual void command(Token command) = 0;

    //    //! If the work command does not provide command to the dep file
    //    //! if true the dep-file-command is added at the "work" stage
    //    virtual bool shouldAddCommandToDepFile() const = 0;
    //    virtual void shouldAddCommandToDepFile(bool value) = 0;
};
