.PHONY: deploy build test argocd-install argocd-password argocd-ui k8s-secrets

deploy:
	bash scripts/deploy.sh

build:
	cd ../.. && mkdir -p build && cd build && \
		cmake .. -DCMAKE_BUILD_TYPE=Release && \
		cmake --build . -j$$(nproc) --target quarto_platform

test:
	cd ../.. && cd build && ctest --test-dir . -R "test_quarto" --output-on-failure

argocd-install:
	kubectl create namespace argocd --dry-run=client -o yaml | kubectl apply -f -
	kubectl apply -n argocd -f https://raw.githubusercontent.com/argoproj/argo-cd/stable/manifests/install.yaml
	@echo "Waiting for ArgoCD to be ready..."
	kubectl wait --for=condition=available --timeout=300s deployment/argocd-server -n argocd
	kubectl apply -f k8s/argocd/appproject.yaml
	kubectl apply -f k8s/argocd/application.yaml
	@echo "ArgoCD installed. Run 'make argocd-password' for initial password."

argocd-password:
	@kubectl -n argocd get secret argocd-initial-admin-secret -o jsonpath="{.data.password}" | base64 -d
	@echo ""

argocd-ui:
	@echo "ArgoCD UI: https://localhost:8443"
	kubectl port-forward svc/argocd-server -n argocd 8443:443

k8s-secrets:
	@echo "Creating gateway secrets..."
	kubectl create secret generic gateway-secrets \
		--from-literal=jwt-secret=$${JWT_SECRET:-dev-secret} \
		--from-literal=github-client-id=$${GITHUB_CLIENT_ID:-test} \
		--from-literal=github-client-secret=$${GITHUB_CLIENT_SECRET:-test} \
		-n quarto --dry-run=client -o yaml | kubectl apply -f -
