#  This shell file gives examples of launching simulatoions for all 40 workloads


#set this variable to NumCores in your cluster machine for faster sims
num_parallel_jobs=8


###########  HOW TO RUN JOBS?  ################

# The following line will launch sims for all workloads when you run ./doit.sh (comment it if you dont want it to) 

 ./runall.pl -f $num_parallel_jobs -d "../results/GSHARE.32KB"



###########  HOW TO GET STATS?  ################

# To compare stats, uncomment the line below after all jobs finish

# ./getdata.pl -d ../results/GSHARE.04KB  ../results/GSHARE.08KB ../results/GSHARE.16KB ../results/GSHARE.32KB



###########  PAST RUNS FOR 4KB, 8KB, and 16KB ###########

# The results are already in "../results" directory

#./runall.pl -f $num_parallel_jobs -d "../results/GSHARE.04KB"
#./runall.pl -f $num_parallel_jobs -d "../results/GSHARE.08KB"
#./runall.pl -f $num_parallel_jobs -d "../results/GSHARE.16KB"

################## GOOD LUCK! ##################
