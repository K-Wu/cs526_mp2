import glob
import os
with open("opt-commands-with-redirection.txt",'w') as fwd:
    for glob_list in [glob.glob("*.ll"),glob.glob("X86/*.ll")]:
        for filename in glob_list:
            with open(filename) as fd:
                for line in fd:
                    line=line.strip()
                    if line.find("RUN")!=-1:
                        print("prob")
                        begIdx = line.find("opt")
                        endIdx = line.rfind("| FileCheck")
                        if endIdx!=-1:
                            line_to_write=line[begIdx:endIdx]
                        else:
                            line_to_write=line[begIdx:]
                        fwd.write(line_to_write.replace("%S","tests-original").replace("%s",os.path.join("tests-original",filename))+" >tests-original/RefResults/{filename}.output  2>tests-original/RefResults/{filename}.output2".format(filename=filename))
                        fwd.write("\n")
                        break