all default: style-fix style-check

style-fix: shell-style-fix
style-check: shell-style-check

SH_SCRIPTS   := $(shell grep -r -l --exclude='#*' --exclude='*~' --exclude='*.tar' --exclude=gradlew --exclude-dir=.git '^\#! \?\(/bin/\|/usr/bin/env \)sh'   | grep -v addrfilter | grep -v cronic-orig | grep -v mail-stackoverflow.sh)
BASH_SCRIPTS := $(shell grep -r -l --exclude='#*' --exclude='*~' --exclude='*.tar' --exclude=gradlew --exclude-dir=.git '^\#! \?\(/bin/\|/usr/bin/env \)bash' | grep -v addrfilter | grep -v cronic-orig | grep -v mail-stackoverflow.sh)
CHECKBASHISMS := $(shell if command -v checkbashisms > /dev/null ; then \
	  echo "checkbashisms" ; \
	else \
	  wget -q -N https://homes.cs.washington.edu/~mernst/software/checkbashisms && \
	  mv checkbashisms .checkbashisms && \
	  chmod +x ./.checkbashisms && \
	  echo "./.checkbashisms" ; \
	fi)
shell-style-fix:
ifneq ($(SH_SCRIPTS)$(BASH_SCRIPTS),)
	@shfmt -w -i 2 -ci -bn -sr ${SH_SCRIPTS} ${BASH_SCRIPTS}
	@shellcheck -x -P SCRIPTDIR --format=diff ${SH_SCRIPTS} ${BASH_SCRIPTS} | patch -p1
endif
shell-style-check:
ifneq ($(SH_SCRIPTS)$(BASH_SCRIPTS),)
	@shfmt -d -i 2 -ci -bn -sr ${SH_SCRIPTS} ${BASH_SCRIPTS}
	@shellcheck -x -P SCRIPTDIR --format=gcc ${SH_SCRIPTS} ${BASH_SCRIPTS}
endif
ifneq ($(SH_SCRIPTS),)
	@${CHECKBASHISMS} -l ${SH_SCRIPTS}
endif
showvars:
	@echo "SH_SCRIPTS=${SH_SCRIPTS}"
	@echo "BASH_SCRIPTS=${BASH_SCRIPTS}"
