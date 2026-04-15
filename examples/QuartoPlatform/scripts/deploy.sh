#!/bin/bash
set -e

REGISTRY="${REGISTRY:-localhost:5000}"
TAG="sha-$(git rev-parse --short HEAD)"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

echo "=== Building quarto-server images (tag: $TAG) ==="

cd "$PROJECT_ROOT"

echo "[1/2] Building gateway..."
docker build \
    --target gateway \
    -t "$REGISTRY/quarto-server:gateway-$TAG" \
    -f examples/QuartoPlatform/Dockerfile .

echo "[2/2] Building editor..."
docker build \
    --target editor \
    -t "$REGISTRY/quarto-server:editor-$TAG" \
    -f examples/QuartoPlatform/Dockerfile .

echo "=== Pushing to $REGISTRY ==="
docker push "$REGISTRY/quarto-server:gateway-$TAG"
docker push "$REGISTRY/quarto-server:editor-$TAG"

echo "=== Updating kustomization ==="
cd "$SCRIPT_DIR/../k8s/overlays/dev"

if command -v kustomize &> /dev/null; then
    kustomize edit set image \
        "quarto-gateway=$REGISTRY/quarto-server:gateway-$TAG" \
        "quarto-editor=$REGISTRY/quarto-server:editor-$TAG"
else
    sed -i "s|newTag: gateway-.*|newTag: gateway-$TAG|" kustomization.yaml
    sed -i "s|newTag: editor-.*|newTag: editor-$TAG|" kustomization.yaml
fi

echo "=== Committing and pushing ==="
cd "$PROJECT_ROOT"
git add examples/QuartoPlatform/k8s/overlays/dev/kustomization.yaml
git commit -m "deploy: update images to $TAG"
git push

echo ""
echo "=== Deployed $TAG ==="
echo "ArgoCD will sync automatically."
echo ""
echo "Gateway: $REGISTRY/quarto-server:gateway-$TAG"
echo "Editor:  $REGISTRY/quarto-server:editor-$TAG"
