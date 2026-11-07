#!/usr/bin/env python
"""
tools/reference/review.py

CLI tool for managing the reference library candidate review process.

Features:
- List candidates waiting for review
- Promote a candidate into the library (moves from candidates/ to library/)
- Demote a library design back to candidate status
- List library designs with tags and status
- Search designs by tag vocabulary
"""
import argparse
import json
import sys
from pathlib import Path

# Add the project root to the path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))



def main():
    parser = argparse.ArgumentParser(description="Reference library review tool")
    subparsers = parser.add_subparsers(dest='command', help='Available commands')
    
    # List candidates
    list_cand_parser = subparsers.add_parser('list-candidates', help='List candidates waiting for review')
    list_cand_parser.add_argument('--path', default='data/reference/candidates', help='Candidates directory path')
    
    # Promote candidate
    promote_parser = subparsers.add_parser('promote', help='Promote a candidate into the library')
    promote_parser.add_argument('candidate_id', help='Candidate ID to promote')
    promote_parser.add_argument('--path', default='data/reference', help='Reference directory path')
    
    # List library designs
    list_lib_parser = subparsers.add_parser('list', help='List library designs')
    list_lib_parser.add_argument('--path', default='data/reference/library', help='Library directory path')
    
    # Search by tag
    search_parser = subparsers.add_parser('search', help='Search designs by tag')
    search_parser.add_argument('tag', help='Tag to search for')
    search_parser.add_argument('--path', default='data/reference/library', help='Library directory path')
    
    args = parser.parse_args()
    
    if args.command == 'list-candidates':
        print("Candidates waiting for review:")
        candidates_path = Path(args.path)
        if candidates_path.exists():
            for f in sorted(candidates_path.glob('*.json')):
                with open(f) as fh:
                    data = json.load(fh)
                print(f"  {f.stem}: {data.get('design_id', 'unknown')}")
        else:
            print("  No candidates directory found")
    
    elif args.command == 'promote':
        candidate_id = args.candidate_id
        print(f"Promoting candidate: {candidate_id}")
        
        # Move candidate record to library
        candidates_path = Path('data/reference/candidates')
        library_path = Path('data/reference/library')
        
        cand_file = candidates_path / (candidate_id + '.json')
        if not candidates_path.exists() or not cand_file.exists():
            print(f"  Error: Candidate {candidate_id} not found in candidates/")
            sys.exit(1)
        
        # Read candidate data
        with open(cand_file) as f:
            candidate_data = json.load(f)
        
        # Write to library
        lib_file = library_path / (candidate_id + '.json')
        with open(lib_file, 'w') as f:
            json.dump(candidate_data, f, indent=2)
        
        # Remove from candidates
        cand_file.unlink()
        
        print(f"  Promoted {candidate_id} to library")
        print(f"  Library now has {len(list(library_path.glob('*.json')))} designs")
    
    elif args.command == 'list':
        print("Library designs:")
        library_path = Path(args.path)
        if library_path.exists():
            for f in sorted(library_path.glob('*.json')):
                with open(f) as fh:
                    data = json.load(fh)
                tags = data.get('tags', [])
                status = 'VERIFIED' if data.get('verified') else 'UNVERIFIED'
                print(f"  {f.stem}: id={data.get('record_id')}, status={status}, tags={tags}")
        else:
            print("  No library directory found")
    
    elif args.command == 'search':
        tag = args.tag
        print(f"Designs with tag '{tag}':")
        library_path = Path(args.path)
        if library_path.exists():
            found = False
            for f in sorted(library_path.glob('*.json')):
                with open(f) as fh:
                    data = json.load(fh)
                if tag in data.get('tags', []):
                    print(f"  {f.stem}: id={data.get('record_id')}, status={data.get('overall_status')}")
                    found = True
            if not found:
                print("  No designs found with that tag")
        else:
            print("  No library directory found")


if __name__ == "__main__":
    main()