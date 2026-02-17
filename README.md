# Implementation and Analysis of a SAT Solver
## By Santiago García Mayayo & Lucas Idsinga
 
This GitHub repository contains C code for a Boolean Satisfiability (SAT) solver. This project was our final project for the course Software Verification at Leiden University taught by Dr. A.W. Laarman.

The project implements a DPLL-based algorithm incorporating unit propagation, pure literal elimination, and the 2-Watched Literals strategy. We focused on creating an efficient search process with a positive memory usage profile by implementing a custom undo stack instead of cloning the formula during backtracking. Most of our methods, particularly the 2-Watched Literals and the "Most Appearances" heuristic, performed quite well in our experiments (see table in docs/ or results section).

![alt text](./results/results.png)

Using these strategies, we aim to determine if there exists an assignment of variables that makes a Boolean formula true. High efficiency is necessary as SAT is an NP-complete problem, and solving large formulas can become computationally expensive without proper optimizations like 2-Watched Literals.

### **Requirements**

The program is implemented in C to ensure predictable and efficient memory usage. The required library (and its used version) that is not part of the standard library is:
- GLib 2.0 (2.75.0 or newer recommended)

On Ubuntu/WSL, you can install the library by running:

```
> sudo apt install libglib2.0-dev
```

### **Usage**

After installing the dependencies, make sure that the folder structure is correct. You can compile and run the solver using the provided Makefile:

```
> make
> ./sat_solver tests/uf50-01.cnf
```
The tests/ folder contains several CNF formulas in DIMACS format. If you wish to run the full battery of tests, use the provided script:
```
> chmod +x run_tests.sh
> ./run_tests.sh
```

### References
<a id="1">[1]</a>
S.A. Cook (1971). The complexity of theorem-proving procedures. Proceedings of the 3rd Annual ACM Symposium on Theory of Computing, 151-158. 

<a id="2">[2]</a>
J. Franco and J. Martin (2021). A history of satisfiability. Handbook of Satisfiability, Second Edition, 3-74. 

<a id="3">[3]</a>
M.W. Moskewicz et al. (2001). Chaff: Engineering an efficient SAT solver. Proceedings of the 38th Design Automation Conference (DAC), 530-535.
