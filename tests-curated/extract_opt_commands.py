import glob
import os
def generate_avx2():
    with open("opt-runs-with-redirection-avx2.txt",'w') as fwd:
        for glob_list in [glob.glob("*.ll")]:
            for filename in glob_list:
                with open(filename) as fd:
                    for line in fd:
                        line=line.strip()
                        if line.find("RUN")!=-1 and line.find("avx2")!=-1:
                            print("prob")
                            begIdx = line.find("opt")
                            endIdx = line.rfind("| FileCheck")
                            if endIdx!=-1:
                                line_to_write=line[begIdx:endIdx]
                            else:
                                line_to_write=line[begIdx:]
                            fwd.write(line_to_write.replace("%S","tests-curated").replace("%s",os.path.join("tests-curated",filename))+" >tests-curated/Output/avx2/{filename}.output  2>tests-curated/Output/avx2/{filename}.output2".format(filename=filename))
                            fwd.write("\n")
                            break

def generate_single_command():
    with open("opt-runs-with-redirection-single.txt",'w') as fwd:
        for glob_list in [glob.glob("*.ll")]:
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
                            fwd.write(line_to_write.replace("%S","tests-curated").replace("%s",os.path.join("tests-curated",filename))+" >tests-curated/Output/SingleCommand/{filename}.output  2>tests-curated/Output/SingleCommand/{filename}.output2".format(filename=filename))
                            fwd.write("\n")
                            break

if __name__=="__main__":
    generate_single_command()