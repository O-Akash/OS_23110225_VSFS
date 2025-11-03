Run the following command to verify testcases

for i in {0..7}; do echo "Running testcase$i..."; gcc -I. testcases/testcase$i.c simplefs-ops.c simplefs-disk.c -o runfs; ./runfs > my_output$i.txt; echo "Comparing outputs for testcase$i..."; if diff my_output$i.txt expected_output/testcase$i.out > /dev/null; then echo "Passed testcase$i"; else echo "Failed testcase$i (see my_output$i.txt for details)"; fi; echo; done
