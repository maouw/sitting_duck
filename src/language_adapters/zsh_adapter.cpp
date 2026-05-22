#include "language_adapter.hpp"
#include "unified_ast_backend_impl.hpp"
#include "semantic_types.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/helper.hpp"
#include <cstring>

// Tree-sitter language declarations
extern "C" {
const TSLanguage *tree_sitter_zsh();
}

namespace duckdb {

//==============================================================================
// Zsh Adapter implementation
//==============================================================================

#define DEF_TYPE(raw_type, semantic_type, name_strat, native_strat, flags)                                             \
	{raw_type, NodeConfig(SemanticTypes::semantic_type, ExtractionStrategy::name_strat,                                \
	                      NativeExtractionStrategy::native_strat, flags)},

const unordered_map<string, NodeConfig> ZshAdapter::node_configs = {
#include "../language_configs/zsh_types.def"
};

#undef DEF_TYPE

string ZshAdapter::GetLanguageName() const {
	return "zsh";
}

vector<string> ZshAdapter::GetAliases() const {
	return {"zsh", "shell", "sh"};
}

void ZshAdapter::InitializeParser() const {
	parser_wrapper_ = make_uniq<TSParserWrapper>();
	auto ts_language = tree_sitter_zsh();
	parser_wrapper_->SetLanguage(ts_language, "Zsh");
}

unique_ptr<TSParserWrapper> ZshAdapter::CreateFreshParser() const {
	auto fresh_parser = make_uniq<TSParserWrapper>();
	auto ts_language = tree_sitter_zsh();
	fresh_parser->SetLanguage(ts_language, "Zsh");
	return fresh_parser;
}

string ZshAdapter::GetNormalizedType(const string &node_type) const {
	const NodeConfig *config = GetNodeConfig(node_type);
	if (config) {
		return SemanticTypes::GetSemanticTypeName(config->semantic_type);
	}
	return node_type; // Fallback to raw type
}

string ZshAdapter::ExtractNodeName(TSNode node, const string &content) const {
	const char *node_type_str = ts_node_type(node);
	const NodeConfig *config = GetNodeConfig(node_type_str);

	if (config) {
		if (config->name_strategy == ExtractionStrategy::CUSTOM) {
			string node_type = string(node_type_str);
			if (node_type == "declaration_command") {
				// declaration_command nests the variable name 2 levels deep:
				// declaration_command -> variable_assignment -> variable_name
				TSNode var_assign = FindChildByTypeNode(node, "variable_assignment");
				if (!ts_node_is_null(var_assign)) {
					return FindChildByType(var_assign, content, "variable_name");
				}
				// Fallback: direct variable_name child (e.g., `local var`)
				string result = FindChildByType(node, content, "variable_name");
				if (!result.empty()) {
					return result;
				}
				// Last resort: find first word child that isn't a flag
				// e.g., `declare -a FILES_PROCESSED` has word children: "-a", "FILES_PROCESSED"
				uint32_t child_count = ts_node_child_count(node);
				for (uint32_t i = 0; i < child_count; i++) {
					TSNode child = ts_node_child(node, i);
					if (string(ts_node_type(child)) == "word") {
						string text = ExtractNodeText(child, content);
						if (!text.empty() && text[0] != '-') {
							return text;
						}
					}
				}
				return "";
			}
		}
		return ExtractByStrategy(node, content, config->name_strategy);
	}

	return "";
}

string ZshAdapter::ExtractNodeValue(TSNode node, const string &content) const {
	const char *node_type_str = ts_node_type(node);
	const NodeConfig *config = GetNodeConfig(node_type_str);

	if (config) {
		// Note: value_strategy is now repurposed as native_strategy for pattern-based extraction
		// For backward compatibility, we'll return empty string since most nodes don't need legacy value extraction
		return "";
	}

	return "";
}

bool ZshAdapter::IsPublicNode(TSNode node, const string &content) const {
	// Zsh doesn't have visibility concepts - all nodes are "public"
	return true;
}

const unordered_map<string, NodeConfig> &ZshAdapter::GetNodeConfigs() const {
	return node_configs;
}

ParsingFunction ZshAdapter::GetParsingFunction() const {
	// Return a lambda that captures the templated parsing function
	return [](const void *adapter, const string &content, const string &language, const string &file_path,
	          int32_t peek_size, const string &peek_mode) -> ASTResult {
		auto typed_adapter = static_cast<const ZshAdapter *>(adapter);
		return UnifiedASTBackend::ParseToASTResultTemplated(typed_adapter, content, language, file_path, peek_size,
		                                                    peek_mode);
	};
}

} // namespace duckdb
