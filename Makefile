# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: your_login <your_login@student.42.fr>      +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2024/01/01 00:00:00 by your_login       #+#    #+#              #
#    Updated: 2024/01/01 00:00:00 by your_login      ###   ########.fr        #
#                                                                              #
# **************************************************************************** #

# ========== VARIABLES ========== #

# Nom de l'exécutable
NAME = ircserv

# Compilateur et flags
CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98

# Dossiers
SRCDIR = src
INCDIR = include
OBJDIR = obj

# Fichiers source
SOURCES = main.cpp \
		  Server.cpp \
		  Client.cpp

# Génération des chemins complets et objets
SRCS = $(addprefix $(SRCDIR)/, $(SOURCES))
OBJS = $(addprefix $(OBJDIR)/, $(SOURCES:.cpp=.o))

# Headers dependencies (pour recompiler si un .hpp change)
HEADERS = $(INCDIR)/Server.hpp \
		  $(INCDIR)/Client.hpp

# ========== RULES ========== #

# Règle par défaut
all: $(NAME)

# Création de l'exécutable
$(NAME): $(OBJS)
	@echo "Linking $(NAME)..."
	@$(CXX) $(CXXFLAGS) -o $(NAME) $(OBJS)
	@echo "✅ $(NAME) compiled successfully!"

# Compilation des objets
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp $(HEADERS) | $(OBJDIR)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -I$(INCDIR) -c $< -o $@

# Création du dossier obj si inexistant
$(OBJDIR):
	@mkdir -p $(OBJDIR)

# Nettoyage des objets
clean:
	@echo "Cleaning object files..."
	@rm -rf $(OBJDIR)
	@echo "🧹 Object files cleaned!"

# Nettoyage complet
fclean: clean
	@echo "Cleaning executable..."
	@rm -f $(NAME)
	@echo "🧹 Executable cleaned!"

# Recompilation complète
re: fclean all

# Test rapide de compilation (sans link)
test:
	@echo "Testing compilation..."
	@$(CXX) $(CXXFLAGS) -I$(INCDIR) -c $(SRCS)
	@rm -f *.o
	@echo "✅ Compilation test passed!"

# Afficher les variables (debug)
debug:
	@echo "NAME: $(NAME)"
	@echo "CXX: $(CXX)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo "SOURCES: $(SOURCES)"
	@echo "SRCS: $(SRCS)"
	@echo "OBJS: $(OBJS)"
	@echo "HEADERS: $(HEADERS)"

# Éviter les conflits avec des fichiers du même nom
.PHONY: all clean fclean re test debug