#!/bin/bash
# GameCube Hardware Testing Script
# This script provides a comprehensive checklist for manual hardware validation

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored output
print_header() {
    echo -e "\n${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}\n"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ $1${NC}"
}

# Verify build artifacts
verify_artifacts() {
    print_header "Step 1: Verifying Build Artifacts"
    
    local artifacts=(
        "OUT/bin/boot.dol"
        "OUT/bin/xash"
        "OUT/bin/gamecube-handoff.txt"
        "OUT/libref_gx.a"
        "OUT/libfilesystem_stdio.a"
        "OUT/valve/extras.pk3"
    )
    
    local all_present=true
    for artifact in "${artifacts[@]}"; do
        if [ -f "$PROJECT_DIR/$artifact" ]; then
            print_success "$artifact exists"
        else
            print_error "$artifact missing"
            all_present=false
        fi
    done
    
    if [ "$all_present" = true ]; then
        print_success "All required artifacts present"
        return 0
    else
        print_error "Some artifacts are missing. Run scripts/build-gamecube.sh first."
        return 1
    fi
}

# Verify artifact checksums
verify_checksums() {
    print_header "Step 2: Verifying Artifact Checksums"
    
    cd "$PROJECT_DIR"
    
    local manifest_file=".ai/logs/hardware-handoff-20260725-070254/artifact-manifest.tsv"
    
    if [ ! -f "$manifest_file" ]; then
        print_warning "Handoff manifest not found, using build verification"
        manifest_file=".ai/logs/hardware-handoff-20260725-070254/artifact-manifest.tsv"
    fi
    
    # Verify boot.dol
    local expected_sha="33d5505489e6143b35a6b957b9ef693d74b5d6db55f06640d81f434ba058a994"
    local actual_sha=$(sha256sum "OUT/bin/boot.dol" | cut -d' ' -f1)
    
    if [ "$actual_sha" = "$expected_sha" ]; then
        print_success "boot.dol checksum verified"
    else
        print_error "boot.dol checksum mismatch!"
        print_info "Expected: $expected_sha"
        print_info "Actual:   $actual_sha"
        return 1
    fi
    
    # Verify xash
    expected_sha="216662ae972185a6b242f6bfa869710def7ac2b2cf82dadd8c098ee1cf01a99c"
    actual_sha=$(sha256sum "OUT/bin/xash" | cut -d' ' -f1)
    
    if [ "$actual_sha" = "$expected_sha" ]; then
        print_success "xash checksum verified"
    else
        print_error "xash checksum mismatch!"
        print_info "Expected: $expected_sha"
        print_info "Actual:   $actual_sha"
        return 1
    fi
    
    return 0
}

# Display hardware testing checklist
show_testing_checklist() {
    print_header "Step 3: Hardware Testing Checklist"
    
    echo "Required Hardware:"
    echo "  - GameCube (or Wii in GameCube mode)"
    echo "  - SD card (for Swiss loader or homebrew SD loader)"
    echo "  - Controller (GameCube controller recommended)"
    echo "  - Video capture setup (optional but recommended)"
    echo "  - Legal Half-Life assets (valve folder)"
    echo ""
    
    echo "SD Card Layout:"
    echo "  sd:/apps/xash3d-gc/boot.dol"
    echo "  sd:/xash3d/valve/          (Half-Life assets)"
    echo "  sd:/xash3d/valve/save/     (for save games)"
    echo "  sd:/xash3d/valve/logs/     (for logs)"
    echo "  sd:/xash3d/valve/screenshots/ (for screenshots)"
    echo ""
    
    echo "Testing Steps:"
    echo ""
    echo "1. Boot Test (HW-BOOT-001)"
    echo "   [ ] Copy boot.dol to sd:/apps/xash3d-gc/boot.dol"
    echo "   [ ] Boot through Swiss loader"
    echo "   [ ] Observe video output"
    echo "   [ ] Record hardware model, video cable, loader version"
    echo "   [ ] Document boot behavior"
    echo ""
    echo "2. Engine Readiness (HW-BOOT-002)"
    echo "   [ ] Wait for engine initialization"
    echo "   [ ] Look for OSReport markers"
    echo "   [ ] Check for 'REF_GX static GetRefAPI' message"
    echo "   [ ] Verify 'retail Flipper policy' markers"
    echo ""
    echo "3. Map Load Test (HW-MAP-001)"
    echo "   [ ] Ensure Half-Life assets are in sd:/xash3d/valve/"
    echo "   [ ] Load map c0a0e (smoke map)"
    echo "   [ ] Verify map loads successfully"
    echo "   [ ] Document any errors"
    echo ""
    echo "4. Player Spawn Test (HW-PLAYER-001)"
    echo "   [ ] Verify player spawns after map load"
    echo "   [ ] Check camera/view initialization"
    echo "   [ ] Observe player model"
    echo ""
    echo "5. Controller Test (HW-INPUT-001)"
    echo "   [ ] Test movement (WASD or D-pad)"
    echo "   [ ] Test look (mouse or right stick)"
    echo "   [ ] Test use/fire buttons"
    echo "   [ ] Test jump button"
    echo "   [ ] Test pause/menu"
    echo "   [ ] Test disconnect/reconnect behavior"
    echo ""
    echo "6. Audio Test (HW-AUDIO-001)"
    echo "   [ ] Listen for audio output"
    echo "   [ ] Test weapon sounds"
    echo "   [ ] Test ambient sounds"
    echo "   [ ] Test menu sounds"
    echo "   [ ] Note any silence or hangs"
    echo ""
    echo "7. Storage Test (HW-FS-001, HW-FS-002)"
    echo "   [ ] Verify SD card is detected"
    echo "   [ ] Check for config file creation"
    echo "   [ ] Test save game creation"
    echo "   [ ] Test save game loading"
    echo "   [ ] Verify directory structure"
    echo ""
    echo "8. Stability Test (HW-STABILITY-001)"
    echo "   [ ] Run for at least 5 minutes"
    echo "   [ ] Monitor for crashes"
    echo "   [ ] Check for thermal issues"
    echo "   [ ] Observe frame pacing"
    echo "   [ ] Test extended gameplay"
    echo ""
}

# Display failure taxonomy
show_failure_taxonomy() {
    print_header "Step 4: Failure Taxonomy"
    
    echo "If testing fails, use one of these labels:"
    echo ""
    echo "  loader_failure              - Loader rejects artifact"
    echo "  no_video                    - No video output"
    echo "  bootstrap_failure           - Boot fails before engine"
    echo "  filesystem_mount_failure    - Storage mount issues"
    echo "  asset_lookup_failure        - Map/asset loading issues"
    echo "  config_write_failure        - Config save issues"
    echo "  save_load_failure           - Save game issues"
    echo "  bsp_load_failure            - BSP parsing issues"
    echo "  entity_spawn_failure        - Entity spawn issues"
    echo "  renderer_failure            - Rendering issues"
    echo "  controller_failure          - Controller input issues"
    echo "  audio_failure               - Audio output issues"
    echo "  memory_pressure             - Memory issues"
    echo "  performance_stall           - Frame pacing issues"
    echo "  bounded_hang                - Timeout with recovery"
    echo "  unbounded_hang              - Infinite hang"
    echo "  crash                       - Unexpected crash"
    echo "  thermal_or_power_issue      - Hardware issues"
    echo "  unknown                     - Unknown cause"
    echo ""
}

# Create evidence template
create_evidence_template() {
    print_header "Step 5: Evidence Template"
    
    local evidence_dir="$PROJECT_DIR/.ai/logs/hardware-handoff-20260725-070254"
    local evidence_file="$evidence_dir/evidence-template.md"
    
    if [ -f "$evidence_file" ]; then
        print_success "Evidence template found at: $evidence_file"
        echo ""
        echo "Template content:"
        echo "----------------"
        cat "$evidence_file"
        echo "----------------"
        echo ""
        print_info "Copy this template and fill in your test results"
        print_info "Add completed evidence to: docs/GAMECUBE_PORT_PLAN.md"
    else
        print_error "Evidence template not found!"
        return 1
    fi
}

# Display build information
show_build_info() {
    print_header "Build Information"
    
    echo "Commit: $(git rev-parse HEAD 2>/dev/null || echo 'unknown')"
    echo "Build Date: $(date -r OUT/bin/boot.dol 2>/dev/null || echo 'unknown')"
    echo ""
    echo "Artifact Sizes:"
    echo "  boot.dol: $(stat -c%s OUT/bin/boot.dol 2>/dev/null || echo 'unknown') bytes"
    echo "  xash: $(stat -c%s OUT/bin/xash 2>/dev/null || echo 'unknown') bytes"
    echo ""
    echo "Handoff Package: .ai/logs/hardware-handoff-20260725-070254/"
    echo ""
}

# Main function
main() {
    print_header "GameCube Hardware Validation Script"
    echo "This script guides you through manual hardware testing"
    echo ""
    
    # Check if we're in the project directory
    if [ ! -d "$PROJECT_DIR/OUT" ]; then
        print_error "Project directory not found!"
        print_info "Run this script from: $PROJECT_DIR"
        exit 1
    fi
    
    # Step 1: Verify artifacts
    verify_artifacts || exit 1
    
    # Step 2: Verify checksums
    verify_checksums || exit 1
    
    # Step 3: Show testing checklist
    show_testing_checklist
    
    # Step 4: Show failure taxonomy
    show_failure_taxonomy
    
    # Step 5: Show evidence template
    create_evidence_template
    
    # Build info
    show_build_info
    
    print_header "Next Steps"
    echo "1. Follow the testing checklist above"
    echo "2. Record your results in the evidence template"
    echo "3. Add completed evidence to docs/GAMECUBE_PORT_PLAN.md"
    echo "4. Update docs/GAMECUBE_PORT_STATUS.md with results"
    echo ""
    print_info "For detailed testing procedures, see docs/GAMECUBE_HARDWARE_VALIDATION.md"
    echo ""
}

main "$@"