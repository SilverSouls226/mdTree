# SPEC.md
## Title: Checklist Generation Feature

## Objective
Add a new feature to `mdtree` that generates a topic list with all the headings converted into a nested checklist, corresponding to the original markdown file's heading hierarchy. 

## Requirements
1. The feature is triggered by the `-f` flag.
2. The `-f` flag takes an optional argument: the output file name.
3. If no output file name is provided, the output file name should default to `[Original File Name] - Checklist.md` (or just append "- Checklist" to the given file name, before the extension if possible, or exactly as `filename - Checklist`).
4. Output should contain nested checklists for nested headings.
   - Example: 
     - `[ ] Heading 1`
       - `[ ] Heading 1.1`
5. The existing `-f` flag (which mapped to `find`) will be replaced or repurposed for this checklist generation as requested by the user.

## Implementation Details
1. Modify `main.cpp`:
   - Change the `-f` flag in `getopt_long` to use optional argument `f::` or handle it manually if required.
   - Create a boolean config `generate_checklist` and a string `checklist_output_file`.
   - Update `Config` struct in `types.h`.
2. Add a new function in `parser.cpp` / `parser.h` to generate the checklist.
   - The checklist generator will read the parsed lines or the file itself and extract all headings, preserving their nesting level.
   - For each heading, output `[spaces]- [ ] Heading Text`.
3. Save the result to the output file.
