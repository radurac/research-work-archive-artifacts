# Research work artifacts archive at [TUM DSE](https://dse.in.tum.de/)

This repository contains the artifacts for BSc/MSc
theses, guided research (GR) and interdisciplinary projects (IDP) at [our
chair](https://dse.in.tum.de/). The main repository for the manuscripts and the presentations is [here](https://github.com/TUM-DSE/research-work-archive).
For open topics take a look at [our chair's
website](https://dse.in.tum.de/thesis/).



Archives:
- [2026](./archive/2026/README.md)
- [2025](./archive/2025/README.md)
- [2024](./archive/2024/README.md)


## Uploading your artifacts to this repository

Read first the instructions on how to submit your work [here](https://github.com/TUM-DSE/research-work-archive/blob/main/README.md)

For the artifacts, we follow the same naming convention and folders hierarchy, please follow the instructions in the main repository.

The folder with your artifact should be named: 
```
<type>_<lastname>/
```

where
* `<type>` is either bsc, msc, idp or gr
* `<lastname>` is your last name

__NOTE: if you directly modified any significantly big software (LLVM/Linux) please refrain from copying all the files to this repository. Take contact with your advisors who will help you with archiving your artifacts__

Special steps to add your research artifact to the archive:

1. Select the appropriate year and semester folder -- __Based on the submission date__
2. Create a folder with the name defined above
3. __Copy__ the files from the repository you used during your thesis into this folder __unless you worked with a very large project__
4. Add a LICENCE file with attribution if you haven't already.
5. Submit a pull request to this repository
6. Copy the link to your folder on this repository and add it in the relevant `README.md` of the [main repository](https://github.com/TUM-DSE/research-work-archive/tree/main/archive)


## Hints on cleanly copying the artifacts

The rationale behind this repository is to keep a record of what you produced during your thesis. Someone else should be able to copy the folder you created on this repository and reproduce your thesis conclusions (building, running and evaluating).
As a result, you should aim to include the following:

* source code
* build system files
* execution scripts
* configuration files
* evaluation scripts
* any other file you find relevant for building, running and evaluating (for example: a `README.md`)

However, you should try to avoid including the following files:

* evaluation results (in any forms: raw, processed, plots, etc.)
* build results (for example: `.o` files in C)
* pdfs
* any other non-essential files or folder (for example: `__pycache__` in Python)

__Hint__: A good practice is to also copy the `.gitignore` file from your repository (if you have one). A default `.gitignore` can be found in archive. It should apply to all subfolders. If you find a rule that is missing from this `gitignore` feel free to add and include it in your pull request

If you are unsure, please consult with your advisor.

## Licencing and attribution

If the repository you are trying to copy does not already contain a licence notice, we ask you to add a licence and attribution since this repository is public.
We ask that you choose an __open source__ licence (e.g. [MIT](https://opensource.org/license/mit), [GPLv2](https://opensource.org/license/gpl-2-0), [Apachev2](https://opensource.org/license/apache-2-0)).
You can find a list of suitable licences with the required text [here](https://opensource.org/licenses).

To add the licence, simply copy the text of the licence into a LICENCE file in your folder. Do not forget to attribute it to you if required by the licence.
