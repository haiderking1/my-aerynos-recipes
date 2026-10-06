# Shortcuts for building and managing these recipes.
# Run `just` to list them. Package names default to the current directory,
# so inside a package folder `just build` works without arguments.

set quiet

local_repo := env('LOCAL_REPO', env('HOME') / '.cache/local_repo/x86_64')
here := file_name(invocation_directory())

# List the available commands
default:
    just --list --unsorted

# Build a package with boulder
build pkg=here:
    cd "{{ justfile_directory() / pkg }}" && rm -f ./*.stone && boulder build -y stone.yaml

# Copy a built package into the local moss repo and re-index it
publish pkg=here:
    rm -f "{{ local_repo }}/{{ pkg }}"-[0-9]*.stone
    cp "{{ justfile_directory() / pkg }}/{{ pkg }}"-[0-9]*.stone "{{ local_repo }}/"
    moss index "{{ local_repo }}" | grep -E '^Indexed' || true
    echo "{{ pkg }} is in the local repo, run 'sudo moss repo update' then install or sync"

# Build a package and put it in the local repo
local pkg=here: (build pkg) (publish pkg)

# Increase a recipe's release number, needed before rebuilding a changed recipe
bump pkg=here:
    cd "{{ justfile_directory() / pkg }}" && boulder recipe bump

# Check upstream for new releases and rebuild (all packages, or the ones given)
update *pkgs:
    "{{ justfile_directory() }}/autoupdate" {{ pkgs }}

# Show what is in the local repo
ls-local:
    ls -1 "{{ local_repo }}" | grep '\.stone$'

# Remove built .stone files from the recipe folders
clean:
    find "{{ justfile_directory() }}" -mindepth 2 -maxdepth 2 -name '*.stone' -delete -print
