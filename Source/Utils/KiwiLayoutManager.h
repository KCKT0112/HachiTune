#pragma once

#include "../JuceHeader.h"
#include <kiwi/kiwi.h>
#include <unordered_map>
#include <memory>

/**
 * JUCE-Kiwi Integration Layer
 *
 * Provides a convenient way to use Kiwi constraint solver with JUCE components.
 * Supports responsive layouts, dynamic constraints, and automatic component positioning.
 *
 * Example usage:
 *
 *   KiwiLayoutManager layout;
 *
 *   // Define variables for component bounds
 *   auto leftVar = layout.createVariable("left");
 *   auto topVar = layout.createVariable("top");
 *   auto widthVar = layout.createVariable("width");
 *   auto heightVar = layout.createVariable("height");
 *
 *   // Add constraints
 *   layout.addConstraint(leftVar == 10);
 *   layout.addConstraint(topVar == 10);
 *   layout.addConstraint(widthVar == 200);
 *   layout.addConstraint(heightVar == 100);
 *
 *   // Apply layout to component
 *   layout.applyToComponent(myComponent, leftVar, topVar, widthVar, heightVar);
 */
class KiwiLayoutManager
{
public:
    KiwiLayoutManager() = default;
    ~KiwiLayoutManager() = default;

    /**
     * Create a named variable for use in constraints.
     */
    kiwi::Variable createVariable(const juce::String& name)
    {
        kiwi::Variable var(name.toStdString());
        variables[name] = var;
        return var;
    }

    /**
     * Get an existing variable by name.
     */
    kiwi::Variable getVariable(const juce::String& name) const
    {
        auto it = variables.find(name);
        if (it != variables.end())
            return it->second;
        return kiwi::Variable();
    }

    /**
     * Add a constraint to the solver.
     */
    void addConstraint(const kiwi::Constraint& constraint)
    {
        try
        {
            solver.addConstraint(constraint);
        }
        catch (const kiwi::DuplicateConstraint&)
        {
            DBG("Warning: Duplicate constraint");
        }
        catch (const kiwi::UnsatisfiableConstraint&)
        {
            DBG("Error: Unsatisfiable constraint");
        }
    }

    /**
     * Remove a constraint from the solver.
     */
    void removeConstraint(const kiwi::Constraint& constraint)
    {
        try
        {
            solver.removeConstraint(constraint);
        }
        catch (const kiwi::UnknownConstraint&)
        {
            DBG("Warning: Unknown constraint");
        }
    }

    /**
     * Update the solver and get new values.
     */
    void updateVariables()
    {
        solver.updateVariables();
    }

    /**
     * Get the value of a variable after solving.
     */
    double getValue(const kiwi::Variable& var) const
    {
        return var.value();
    }

    /**
     * Apply layout to a JUCE component.
     * Automatically rounds values to integers for pixel-perfect rendering.
     */
    void applyToComponent(juce::Component* component,
                         const kiwi::Variable& left,
                         const kiwi::Variable& top,
                         const kiwi::Variable& width,
                         const kiwi::Variable& height)
    {
        if (component == nullptr)
            return;

        updateVariables();

        int x = juce::roundToInt(left.value());
        int y = juce::roundToInt(top.value());
        int w = juce::roundToInt(width.value());
        int h = juce::roundToInt(height.value());

        component->setBounds(x, y, w, h);
    }

    /**
     * Helper: Create variables for a component's bounds.
     * Returns {left, top, width, height} variables.
     */
    struct ComponentVars
    {
        kiwi::Variable left;
        kiwi::Variable top;
        kiwi::Variable width;
        kiwi::Variable height;
        kiwi::Variable right;   // Computed: left + width
        kiwi::Variable bottom;  // Computed: top + height
        kiwi::Variable centerX; // Computed: left + width/2
        kiwi::Variable centerY; // Computed: top + height/2
    };

    ComponentVars createComponentVars(const juce::String& prefix)
    {
        ComponentVars vars;
        vars.left = createVariable(prefix + "_left");
        vars.top = createVariable(prefix + "_top");
        vars.width = createVariable(prefix + "_width");
        vars.height = createVariable(prefix + "_height");
        vars.right = createVariable(prefix + "_right");
        vars.bottom = createVariable(prefix + "_bottom");
        vars.centerX = createVariable(prefix + "_centerX");
        vars.centerY = createVariable(prefix + "_centerY");

        // Add computed constraints
        addConstraint(vars.right == vars.left + vars.width);
        addConstraint(vars.bottom == vars.top + vars.height);
        addConstraint(vars.centerX == vars.left + vars.width / 2.0);
        addConstraint(vars.centerY == vars.top + vars.height / 2.0);

        return vars;
    }

    /**
     * Apply component vars to a JUCE component.
     */
    void applyComponentVars(juce::Component* component, const ComponentVars& vars)
    {
        applyToComponent(component, vars.left, vars.top, vars.width, vars.height);
    }

    /**
     * Clear all constraints and variables.
     */
    void clear()
    {
        solver.reset();
        variables.clear();
    }

    /**
     * Get the underlying Kiwi solver for advanced usage.
     */
    kiwi::Solver& getSolver() { return solver; }
    const kiwi::Solver& getSolver() const { return solver; }

private:
    kiwi::Solver solver;
    std::unordered_map<juce::String, kiwi::Variable> variables;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KiwiLayoutManager)
};

