To compile this project, run the make command in this directory.

To run the project, run "./oss [-h] [-n proc] [-s simul] [-t timelimit] [-f outputFile]" where:
  -h (optional) shows a help message
  -n (optional) is the total number of child processes to create
  -s (optional) is the maximum number of concurrent child processes
  -t (optional) is the maximum number of seconds each child process should run for
  -f (optional) is the filename to output the PCB tables to

Example 1: "./oss" (defaults -n and -s to 1 and -t to 5 and -f to "output.txt")
Example 2: "./oss -n 14 -s 2 -t 10 -f asdf.txt"