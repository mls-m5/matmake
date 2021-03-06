// Copyright Mattias Larsson Sköld 2019

#pragma once

#include "environment/ienvironment.h"

#include "globals.h"
#include "main/matmake.h"
#include "main/token.h"
#include "target/buildtarget.h"
#include "target/ibuildtarget.h"
#include "target/targetproperties.h"
#include "target/targets.h"
#include "threadpool.h"

#include <fstream>

class Environment : public IEnvironment {
public:
    struct ExternalMatmakeType {
        bool compileBefore;
        Token name;
        Tokens arguments;
    };

    Targets _targets;
    ThreadPool _tasks;
    std::shared_ptr<IFiles> _fileHandler;
    std::vector<ExternalMatmakeType> externalDependencies;

    void addExternalDependency(bool shouldCompileBefore,
                               const Token &name,
                               const Tokens &args) override {
        externalDependencies.push_back({shouldCompileBefore, name, args});
    }

    void buildExternal(bool isBefore, std::string operation) {
        using namespace std;
        for (auto &dependency : externalDependencies) {
            if (dependency.compileBefore != isBefore) {
                continue;
            }

            auto currentDirectory = _fileHandler->currentDirectory();

            if (!_fileHandler->currentDirectory(dependency.name)) {
                vector<string> newArgs(dependency.arguments.begin(),
                                       dependency.arguments.end());

                if (operation == "clean") {
                    cout << endl
                         << "cleaning external " << dependency.name << endl;
                    start({"clean"});
                }
                else {
                    cout << endl
                         << "Building external " << dependency.name << endl;
                    start(newArgs);
                    if (globals.bailout) {
                        break;
                    }
                    cout << endl;
                }
            }
            else {
                throw runtime_error("could not external directory " +
                                    dependency.name);
            }

            if (_fileHandler->currentDirectory(currentDirectory)) {
                throw runtime_error(
                    "could not go back to original working directory " +
                    currentDirectory);
            }
            else {
                vout << "returning to " << currentDirectory << endl;
                vout << endl;
            }
        }
    }

    Environment(std::shared_ptr<IFiles> fileHandler)
        : _fileHandler(fileHandler) {}

    //! Transfer all information parsed from the matmake-file
    void setTargetProperties(TargetPropertyCollection properties) {
        for (auto &property : properties) {
            bool isRoot = property->name() == "root";
            auto target = std::make_unique<BuildTarget>(move(property));
            if (isRoot) {
                _targets.root = target.get();
            }
            _targets.push_back(move(target));
        }
    }

    void print() {
        for (auto &v : _targets) {
            v->print();
        }
    }

    std::vector<IBuildTarget *> parseTargetArguments(
        std::vector<std::string> targetArguments) const {
        std::vector<IBuildTarget *> selectedTargets;

        if (targetArguments.empty()) {
            for (auto &target : _targets) {
                selectedTargets.push_back(target.get());
            }
        }
        else {
            for (auto t : targetArguments) {
                bool matchFailed = true;
                auto target = _targets.find(t);
                if (target) {
                    selectedTargets.push_back(target);
                    matchFailed = false;
                }
                else {
                    std::cout << "target '" << t << "' does not exist\n";
                }

                if (matchFailed) {
                    std::cout << "run matmake --help for help\n";
                    std::cout << "targets: ";
                    listAlternatives();
                    throw std::runtime_error("error when parsing targets");
                }
            }
        }

        return selectedTargets;
    }

    BuildRuleList calculateDependencies(
        std::vector<IBuildTarget *> selectedTargets) const {
        BuildRuleList files;
        for (auto target : selectedTargets) {
            dout << "target " << target->name() << " src "
                 << target->properties().get("src").concat() << std::endl;

            auto targetDependencies =
                target->calculateDependencies(*_fileHandler, _targets);

            typedef decltype(files)::iterator iter_t;

            files.insert(files.end(),
                         std::move_iterator<iter_t>(targetDependencies.begin()),
                         std::move_iterator<iter_t>(targetDependencies.end()));
        }

        return files;
    }

    void prescan(const BuildRuleList &files) const {
        for (auto &file : files) {
            file->prescan(*_fileHandler, files);
        }
    }

    static void printTree(IDependency &dependency, int depth = 1) {
        for (auto &dep : dependency.dependencies()) {
            std::cout << std::string(depth * 2, ' ');
            std::cout << dep->output();
            std::cout << (dep->dirty() ? " (dirty)" : " (clean)") << "\n";
            if (dep->dependencies().empty()) {
                std::cout << std::string(depth * 2 + 2, ' ') << dep->input()
                          << "\n";
            }
            printTree(*dep, depth + 1);
        }
    }

    void printTree() const {
        if (globals.debugOutput) {
            for (auto &target : _targets) {
                std::cout << "tree root target: " << target->name()
                          << " -----------\n";
                if (auto output = target->outputFile()) {
                    std::cout << output->output()
                              << (output->dirty() ? " (dirty)" : " (clean)")
                              << "\n";
                    printTree(*output);
                }
            }
            std::cout << "-----------------------------------" << std::endl;
        }
    }

    void createDirectories(const BuildRuleList &files) const {
        std::set<std::string> directories;
        for (auto &file : files) {
            auto dir = _fileHandler->getDirectory(file->dependency().output());
            if (!dir.empty()) {
                directories.emplace(dir);
            }
        }

        for (auto dir : directories) {
            dout << "output dir: " << dir << std::endl;
            if (dir.empty()) {
                continue;
            }
            if (_fileHandler->isDirectory(dir)) {
                continue; // Skip if already created
            }
            _fileHandler->createDirectory(dir);
        }
    }

    void compile(std::vector<std::string> targetArguments) override {
        dout << "compiling..." << std::endl;
        print();

        auto files =
            calculateDependencies(parseTargetArguments(targetArguments));

        createDirectories(files);

        prescan(files);

        for (auto &file : files) {
            file->prepare(*_fileHandler, files);
        }

        printTree();

        for (auto &file : files) {
            file->dependency().prune();
            if (file->dependency().dirty()) {
                dout << "file " << file->dependency().output() << " is dirty"
                     << std::endl;
                _tasks.addTaskCount();
                if (file->dependency().dependencies().empty()) {
                    _tasks.addTask(&file->dependency());
                }
            }
            else {
                dout << "file " << file->dependency().output() << " is fresh"
                     << std::endl;
            }
        }
        buildExternal(true, "");
        work(std::move(files));
        buildExternal(false, "");
    }

    void runTests(std::vector<std::string> targetArguments) override {
        struct TestInfo {
            std::string name;
            std::string dir;
            bool failed = false;
        };

        std::vector<TestInfo> tests;

        for (auto target : parseTargetArguments(targetArguments)) {
            if (target->buildType() == Test) {
                if (target->name() != "root") {
                    tests.push_back(
                        {target->filename(), target->getOutputDir()});
                }
            }
        }

        auto oldDir = _fileHandler->currentDirectory();

        size_t numFailed = 0;

        std::cout << "\nRunning tests:\n";

        for (auto &test : tests) {
            _fileHandler->currentDirectory(oldDir);
            _fileHandler->currentDirectory(test.dir);
            auto res = _fileHandler->popenWithResult("./" + test.name);
            if (res.first) {
                std::cerr << "\nTest " << test.name << " failed:\n";
                std::cerr << res.second << std::endl;
                ++numFailed;
                test.failed = true;
            }
            else {
                std::cout << "Test " << test.name << ": succeded" << std::endl;
            }
        }

        _fileHandler->currentDirectory(oldDir);

        std::cout << "\nTest summary:\n";

        auto fillStr = [](size_t size, size_t max) {
            using namespace std::literals;
            if (size > max) {
                return ""s;
            }
            else {
                return std::string(max - size, '.');
            }
        };

        for (auto &test : tests) {
            auto fill = fillStr(test.name.size(), 50);
            if (test.failed) {
                std::cout << test.name << " " << fill << " "
                          << "Failed" << std::endl;
            }
            else {
                std::cout << test.name << " " << fill << " "
                          << "Succeded" << std::endl;
            }
        }

        std::cout << "\n"
                  << numFailed << " of " << tests.size() << " tests failed "
                  << std::endl;

        if (numFailed) {
            globals.bailout = true;
        }
    }

    void work(BuildRuleList files) {
        _tasks.work(std::move(files), *_fileHandler);
    }

    void clean(std::vector<std::string> targetArguments) override {
        dout << "cleaning " << std::endl;
        auto files =
            calculateDependencies(parseTargetArguments(targetArguments));

        if (targetArguments.empty()) {
            buildExternal(true, "clean");

            for (auto &file : files) {
                file->dependency().clean(*_fileHandler);
            }

            buildExternal(false, "clean");
        }
        else {
            for (auto &file : files) {
                file->dependency().clean(*_fileHandler);
            }
        }
    }

    //! Show info of alternative build targets
    void listAlternatives() const override {
        for (auto &t : _targets) {
            if (t->name() != "root") {
                std::cout << t->name() << " ";
            }
        }
        std::cout << "clean\n";
    }
};
