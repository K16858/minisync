all: client server

client: client.c protocol.c utils.c
	gcc client.c protocol.c utils.c -o client

server: server.c protocol.c utils.c
	gcc server.c protocol.c utils.c -o server

test:
	@bash test.sh

clean-test:
	@rm -f server_test.log server.pid test_output.csv

clean: clean-test
	@rm -f client server manager
	@rm -rf .msync

install:
	@echo "Installing minisync..."
	@mkdir -p $(HOME)/.local/bin
	@cp client $(HOME)/.local/bin/msync
	@cp server $(HOME)/.local/bin/msync-server
	@chmod +x $(HOME)/.local/bin/msync
	@chmod +x $(HOME)/.local/bin/msync-server
	@echo "✓ Installed to ~/.local/bin/"
	@echo ""
	@if echo $$PATH | grep -q "$(HOME)/.local/bin"; then \
		echo "✓ PATH is already configured"; \
	else \
		echo "⚠ WARNING: ~/.local/bin is NOT in your PATH"; \
		echo ""; \
		echo "Add this line to your shell configuration:"; \
		if [ -f $(HOME)/.bashrc ]; then \
			echo "  echo 'export PATH=\"\$$HOME/.local/bin:\$$PATH\"' >> ~/.bashrc"; \
		elif [ -f $(HOME)/.zshrc ]; then \
			echo "  echo 'export PATH=\"\$$HOME/.local/bin:\$$PATH\"' >> ~/.zshrc"; \
		else \
			echo "  export PATH=\"\$$HOME/.local/bin:\$$PATH\""; \
		fi; \
		echo ""; \
		echo "Then reload your shell:"; \
		echo "  source ~/.bashrc  # or ~/.zshrc"; \
	fi
	@echo ""
	@echo "Quick start:"
	@echo "  1. cd /path/to/your/project"
	@echo "  2. msync init my-project"
	@echo "  3. msync-server (on one machine)"
	@echo "  4. msync discover (on another machine)"

uninstall:
	@echo "Uninstalling minisync..."
	@rm -f $(HOME)/.local/bin/msync
	@rm -f $(HOME)/.local/bin/msync-server
	@echo "Uninstalled from ~/.local/bin/"
	@echo ""
	@echo "To remove all data:"
	@echo "  rm -rf ~/.config/minisync"

.PHONY: test clean-test clean install uninstall